#include "network/adaptive_controller.hpp"
#include "network/tasked_send_receiver.hpp"
#include <algorithm>
#include <cmath>
#include <thread>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2026
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
AdaptiveController::AdaptiveController(const Config& config, unsigned maxRequestsPerThread, double attainment)
// Constructor that seeds the recommendation from the static model
{
    auto hardwareThreads = max(1u, thread::hardware_concurrency());
    _bounds = {0, attainment, max(1u, maxRequestsPerThread), hardwareThreads};
    if (config.bandwidth() > 0) {
        _bounds.targetBytesPerSec = static_cast<double>(config.bandwidth()) * 1000 * 1000 / 8;
        _current.threads = min(static_cast<unsigned>(config.retrievers()), hardwareThreads);
        _current.requestsPerThread = min(config.coreRequests(), _bounds.maxRequests);
    } else {
        // Unknown bandwidth starts small and climbs to the measured plateau
        _current.threads = max(1u, hardwareThreads / 8);
        _current.requestsPerThread = min(static_cast<unsigned>(Config::defaultCoreConcurrency), _bounds.maxRequests);
    }
    _previous = _current;
}
//---------------------------------------------------------------------------
AdaptiveController::Recommendation AdaptiveController::recommend(const TaskedSendReceiverGroup& group)
// Caller driven tick that limits itself to one control decision per epoch
{
    auto now = chrono::steady_clock::now();
    if (!_epoch) {
        _epoch = {now, group.getTransferredBytes()};
        return _current;
    }
    auto elapsed = now - _epoch->start;
    if (elapsed < epochLength)
        return _current;

    auto bytes = group.getTransferredBytes();
    Sample sample = {bytes - _epoch->transferredBytes, chrono::duration_cast<chrono::nanoseconds>(elapsed), group.getQueuedMessages(), group.getInflightMessages()};
    _epoch = {now, bytes};
    return update(sample);
}
//---------------------------------------------------------------------------
AdaptiveController::Recommendation AdaptiveController::update(const Sample& sample)
// The deterministic control law for one epoch sample
{
    auto seconds = chrono::duration<double>(sample.elapsed).count();
    auto bps = seconds > 0 ? static_cast<double>(sample.transferredBytes) / seconds : 0.0;
    auto targetMet = (_bounds.targetBytesPerSec > 0) && (bps >= _bounds.attainment * _bounds.targetBytesPerSec);
    auto total = static_cast<uint64_t>(_current.threads) * _current.requestsPerThread;
    auto demand = sample.queuedMessages > 0 || sample.inflightMessages >= total;

    // Without demand release the threads
    if (!sample.transferredBytes && !sample.queuedMessages && !sample.inflightMessages) {
        if (_phase != Phase::Idle) {
            _current = _previous;
            _current.threads = 1;
            _phase = Phase::Idle;
        }
        return _current;
    }
    // Reattach to the last kept configuration when demand returns
    if (_phase == Phase::Idle) {
        _current = _previous;
        _phase = Phase::Hold;
        _probe.since = 0;
        _trend.lastBps = bps;
        return _current;
    }

    // Judge the last step against the set point
    if (_current != _previous) {
        auto reference = _bounds.targetBytesPerSec > 0 ? min(_bounds.targetBytesPerSec, max(_trend.plateauBps, bps)) : _trend.plateauBps;
        auto keep = _trend.stepUp ? targetMet || bps >= (1 + tolerance) * _trend.lastBps
                                  : bps >= _bounds.attainment * reference;
        if (!keep) {
            _current = _previous;
            if (_trend.stepUp) {
                // A failed growth step marks the plateau
                _phase = Phase::Hold;
                _trend.plateauBps = max(bps, _trend.lastBps);
                _probe.since = 0;
            }
            _trend.lastBps = bps;
            return _current;
        }
        _previous = _current;
        if (_trend.stepUp)
            _phase = Phase::Grow;
    }
    _trend.plateauBps = max(_trend.plateauBps, bps);

    // Hold on attainment, climb again when dropping off the plateau
    if (targetMet || !demand)
        _phase = Phase::Hold;
    else if (_phase == Phase::Hold && bps < (1 - tolerance) * _trend.plateauBps)
        _phase = Phase::Grow;

    switch (_phase) {
        case Phase::Grow: {
            // Jump to the measured demand with damping
            auto desiredExact = static_cast<double>(total) * growthFactor;
            if (_bounds.targetBytesPerSec > 0 && bps > 0) {
                auto active = static_cast<double>(sample.queuedMessages ? total : max<uint64_t>(1, min(total, sample.inflightMessages)));
                desiredExact = min(_bounds.targetBytesPerSec * active / bps, desiredExact);
            }
            auto desired = min(static_cast<uint64_t>(ceil(desiredExact)), static_cast<uint64_t>(_bounds.maxThreads) * _bounds.maxRequests);
            if (desired <= total) {
                _phase = Phase::Hold;
                break;
            }
            // Pack the budget into minimal threads
            _current.threads = clamp(static_cast<unsigned>((desired + _bounds.maxRequests - 1) / _bounds.maxRequests), 1u, _bounds.maxThreads);
            _current.requestsPerThread = clamp(static_cast<unsigned>((desired + _current.threads - 1) / _current.threads), 1u, _bounds.maxRequests);
            _trend.stepUp = true;
            break;
        }
        case Phase::Hold:
            // Probes alternate between growing into a backlog and shedding
            if (++_probe.since >= probeInterval) {
                _probe.since = 0;
                if (_probe.up && !targetMet && sample.queuedMessages > 0) {
                    _phase = Phase::Grow;
                } else if (_current.threads > 1) {
                    _current.threads--;
                    _trend.stepUp = false;
                } else if (_current.requestsPerThread > 1) {
                    _current.requestsPerThread--;
                    _trend.stepUp = false;
                }
                _probe.up = !_probe.up;
            }
            break;
        case Phase::Idle:
            break;
    }
    _trend.lastBps = bps;
    return _current;
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
