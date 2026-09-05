#include "network/adaptive_controller.hpp"
#include "network/config.hpp"
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
static unsigned increase(unsigned value, unsigned fraction, unsigned bound)
// Raise by a fraction of itself, never above the bound
{
    return min(value + max(1u, value / fraction), bound);
}
//---------------------------------------------------------------------------
static unsigned decrease(unsigned value, unsigned fraction, unsigned bound)
// Lower by a fraction of itself, never below the bound
{
    return value - min(value - bound, max(1u, value / fraction));
}
//---------------------------------------------------------------------------
AdaptiveController::AdaptiveController(unsigned maxRequestsPerThread, unsigned seed, unsigned hardware)
// Constructor
{
    auto hardwareThreads = hardware ? hardware : max(1u, thread::hardware_concurrency());
    _bounds = {max(1u, maxRequestsPerThread), hardwareThreads};
    _current.threads = seed ? min(seed, hardwareThreads) : max(1u, hardwareThreads / 8);
    _current.requestsPerThread = max(1u, _bounds.maxRequests / 2);
    _previous = _current;
}
//---------------------------------------------------------------------------
unsigned AdaptiveController::seedThreads(const Config& config, bool tls, unsigned hardwareThreads)
// Starting threads for a known instance
{
    auto threads = max(1u, hardwareThreads);
    if (!config.network || !config.coreThroughput)
        return max(1u, threads / 8);
    // Plain HTTP roughly doubles the modeled TLS throughput
    auto seed = static_cast<double>(config.retrievers()) * (tls ? 1.0 : 0.5);
    return clamp(static_cast<unsigned>(llround(seed)), 1u, threads);
}
//---------------------------------------------------------------------------
AdaptiveController::Recommendation AdaptiveController::recommend(const TaskedSendReceiverGroup& group)
/// Recommnedation tick
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
    return measure(sample);
}
//---------------------------------------------------------------------------
AdaptiveController::Recommendation AdaptiveController::measure(const Sample& sample)
// Measure and react
{
    auto seconds = chrono::duration<double>(sample.elapsed).count();
    auto throughput = seconds > 0 ? static_cast<double>(sample.transferredBytes) / seconds : 0.0;
    auto total = static_cast<uint64_t>(_current.threads) * _current.requestsPerThread;
    // Require settled throughput before probing growth
    auto stable = _statistics.lastThroughput > 0 && abs(throughput - _statistics.lastThroughput) <= tolerance * _statistics.lastThroughput;

    // Without demand reduce the threads
    if (!sample.transferredBytes && !sample.queuedMessages && !sample.inflightMessages) {
        if (_phase != Phase::Idle) {
            _current = _previous;
            _current.threads = 1;
            _phase = Phase::Idle;
        }
        return _current;
    }
    // Reuse best configuration
    if (_phase == Phase::Idle) {
        _current = _previous;
        _phase = Phase::Hold;
        _schedule.since = 0;
        _statistics.lastThroughput = throughput;
        return _current;
    }

    /// Advances the probe rotation
    auto nextProbe = [](Probe probe) {
        return static_cast<Probe>((static_cast<uint8_t>(probe) + 1) % (static_cast<uint8_t>(Probe::GrowThreads) + 1));
    };

    /// Raise the requests per thread
    auto raiseRequests = [&]() {
        if (!_schedule.requestsUp || _current.requestsPerThread >= _bounds.maxRequests)
            return false;
        _current.requestsPerThread = increase(_current.requestsPerThread, 2, _bounds.maxRequests);
        _statistics.lastMove = Move::RequestsUp;
        return true;
    };

    /// Whether the last step earned its concurrency
    auto worthKeeping = [&]() {
        // Request changes must improve throughput
        if (_statistics.lastMove == Move::RequestsUp || _statistics.lastMove == Move::RequestsDown)
            return throughput >= (1 + tolerance) * _statistics.lastThroughput;
        // Thread growth must pay in proportion to the concurrency it adds
        if (_statistics.lastMove == Move::ThreadsUp) {
            auto previous = static_cast<double>(_previous.threads) * _previous.requestsPerThread;
            auto band = max(tolerance, efficiency * abs((static_cast<double>(total) - previous) / previous));
            return throughput >= (1 + band) * _statistics.lastThroughput;
        }
        // Giving concurrency back may not cost throughput
        return throughput >= (1 - tolerance) * _statistics.bestThroughput;
    };

    /// Return to the last kept configuration and learn from the rejection
    auto rejectStep = [&]() {
        _current = _previous;
        _schedule.since = 0;
        _schedule.probe = nextProbe(_schedule.probe);
        switch (_statistics.lastMove) {
            case Move::RequestsUp:
                _schedule.requestsUp = false;
                break;
            case Move::RequestsDown:
                _schedule.requestsUp = true;
                break;
            case Move::Repacked:
                _schedule.repack = false;
                break;
            case Move::ThreadsUp:
                _phase = Phase::Hold;
                _schedule.requestsUp = true;
                _statistics.bestThroughput = _statistics.lastThroughput;
                break;
            case Move::ThreadsDown:
            case Move::None:
                break;
        }
    };

    /// Keep the step
    auto acceptStep = [&]() {
        _previous = _current;
        if (_statistics.lastMove == Move::ThreadsUp)
            _schedule.repack = true;
    };

    /// Add concurrency, requests before threads
    auto growStep = [&]() {
        if (raiseRequests())
            return;
        auto grown = increase(_current.threads, 4, _bounds.maxThreads);
        if (grown <= _current.threads) {
            _phase = Phase::Hold;
            return;
        }
        _current.threads = grown;
        _statistics.lastMove = Move::ThreadsUp;
    };

    /// Fine-grained probes rotate through the knobs during hold
    auto holdProbe = [&]() {
        if (++_schedule.since < probeInterval)
            return;
        // Only giving threads back is safe on unsettled throughput
        if (_schedule.probe != Probe::DecreaseThreads && !stable)
            return;
        switch (_schedule.probe) {
            case Probe::DecreaseThreads:
                // Reduce threads in proportion to their count
                _current.threads = decrease(_current.threads, 4, 1);
                _statistics.lastMove = Move::ThreadsDown;
                break;
            case Probe::MoveRequests:
                // Probe requests in the learned direction
                if (_schedule.requestsUp) {
                    raiseRequests();
                } else {
                    _current.requestsPerThread = decrease(_current.requestsPerThread, 4, 1);
                    _statistics.lastMove = Move::RequestsDown;
                }
                break;
            case Probe::Repack: {
                // Preserve concurrency on fewer threads
                auto packed = max(1u, static_cast<unsigned>((total + _bounds.maxRequests - 1) / _bounds.maxRequests));
                if (_schedule.repack && packed < _current.threads) {
                    _current.threads = packed;
                    _current.requestsPerThread = _bounds.maxRequests;
                    _statistics.lastMove = Move::Repacked;
                }
                break;
            }
            case Probe::GrowThreads:
                // Add a thread only for a queued backlog
                if (sample.queuedMessages > 0) {
                    _current.threads = min(_current.threads + 1, _bounds.maxThreads);
                    _statistics.lastMove = Move::ThreadsUp;
                }
                break;
        }
        if (_current == _previous) {
            _schedule.since = 0;
            _schedule.probe = nextProbe(_schedule.probe);
        }
    };

    auto stepped = _current != _previous;
    if (stepped && !worthKeeping()) {
        rejectStep();
        _statistics.lastThroughput = throughput;
        return _current;
    }
    if (stepped)
        acceptStep();
    _statistics.bestThroughput = max(_statistics.bestThroughput, throughput);

    // Climb again when dropping off bestThroughput
    auto demand = sample.queuedMessages > 0 || sample.inflightMessages >= total;
    if (!demand)
        _phase = Phase::Hold;
    else if (_phase == Phase::Hold && stable && max(throughput, _statistics.lastThroughput) < (1 - tolerance) * _statistics.bestThroughput)
        _phase = Phase::Grow;

    switch (_phase) {
        case Phase::Grow:
            if (stable)
                growStep();
            break;
        case Phase::Hold:
            holdProbe();
            break;
        case Phase::Idle:
            break;
    }
    _statistics.lastThroughput = throughput;
    return _current;
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
