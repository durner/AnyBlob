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
static unsigned increase(unsigned value, unsigned divisor, unsigned bound)
// Raise by a fraction of itself
{
    return min(value + max(1u, value / divisor), bound);
}
//---------------------------------------------------------------------------
static unsigned decrease(unsigned value, unsigned divisor, unsigned bound)
// Lower by a fraction of itself
{
    return value - min(value - bound, max(1u, value / divisor));
}
//---------------------------------------------------------------------------
static unsigned seedThreads(const Config& config, bool tls, unsigned hardwareThreads)
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
AdaptiveController::AdaptiveController(const Config& config, bool tls, unsigned hardware)
// Constructor
{
    _maxThreads = hardware ? hardware : max(1u, thread::hardware_concurrency());
    _current.threads = seedThreads(config, tls, _maxThreads);
    _current.requestsPerThread = max(1u, config.coreRequests());
    _kept = _current;
}
//---------------------------------------------------------------------------
AdaptiveController::Recommendation AdaptiveController::recommend(const TaskedSendReceiverGroup& group, unsigned runningThreads)
/// Update the recommendation
{
    _current.requestsPerThread = min(_current.requestsPerThread, max(1u, TaskedSendReceiverGroup::maxConcurrentRequests));
    auto now = chrono::steady_clock::now();
    if (!_epoch) {
        _epoch = {now, group.getTransferredBytes()};
        return _current;
    }
    auto elapsed = now - _epoch->start;
    if (elapsed < epochLength)
        return _current;

    auto bytes = group.getTransferredBytes();
    Sample sample = {bytes - _epoch->transferredBytes, chrono::duration_cast<chrono::nanoseconds>(elapsed), group.getQueuedMessages(), group.getInflightMessages(), {runningThreads, group.getConcurrentRequests()}, TaskedSendReceiverGroup::maxConcurrentRequests};
    _epoch = {now, bytes};
    return measure(sample);
}
//---------------------------------------------------------------------------
AdaptiveController::Recommendation AdaptiveController::measure(const Sample& sample)
// Measure and react
{
    auto maxRequests = max(1u, sample.maxRequestsPerThread);
    _current.requestsPerThread = min(_current.requestsPerThread, maxRequests);
    _kept.requestsPerThread = min(_kept.requestsPerThread, maxRequests);
    auto seconds = chrono::duration<double>(sample.elapsed).count();
    auto throughput = seconds > 0 ? static_cast<double>(sample.transferredBytes) / seconds : 0.0;
    // Reductions must preserve throughput; increases must improve it
    auto lowers = _step == Step::ThreadsDown || _step == Step::RequestsDown;

    /// Try the next adjustment
    auto rotateStep = [&]() {
        _step = static_cast<Step>((static_cast<uint8_t>(_step) + 1) % (static_cast<uint8_t>(Step::RequestsDown) + 1));
    };

    /// Apply the step; return false at the limit
    auto stepSwitch = [&]() {
        constexpr auto stepFraction = 8;
        switch (_step) {
            // Requests need no extra core, so increase them faster
            case Step::RequestsUp: _current.requestsPerThread = increase(_current.requestsPerThread, stepFraction >> 1, maxRequests); break;
            case Step::ThreadsUp: _current.threads = increase(_current.threads, stepFraction, _maxThreads); break;
            case Step::ThreadsDown: {
                // Redistribute requests across the remaining threads, or decline to keep the concurrency
                auto concurrency = static_cast<uint64_t>(_current.threads) * _current.requestsPerThread;
                auto packed = decrease(_current.threads, stepFraction, 1);
                auto requests = (concurrency + packed - 1) / packed;
                if (packed < _current.threads && requests <= maxRequests) {
                    _current.threads = packed;
                    _current.requestsPerThread = static_cast<unsigned>(requests);
                }
                break;
            }
            case Step::RequestsDown: _current.requestsPerThread = decrease(_current.requestsPerThread, stepFraction, 1); break;
        }
        return _current != _kept;
    };

    // Use one thread while idle
    if (!sample.transferredBytes && !sample.queuedMessages && !sample.inflightMessages) {
        if (!_parked) {
            _current = _kept;
            _current.threads = 1;
            _parked = true;
        }
        return _current;
    }
    // Restore the kept configuration
    if (_parked) {
        _current = _kept;
        _parked = false;
        _since = 0;
        return _current;
    }

    // Wait for a full epoch with this recommendation
    if (sample.applied != _current || !_since++)
        return _current;

    auto won = false;
    if (_current != _kept) {
        // Compare throughput with the kept configuration
        if (throughput < (lowers ? 1 - (tolerance / 2) : 1 + tolerance) * _keptThroughput) {
            _current = _kept;
            _since = 0;
            rotateStep();
            return _current;
        }
        // Keep successful steps
        _kept = _current;
        won = true;
    }
    _keptThroughput = 0.5 * throughput + 0.5 * _keptThroughput;
    if (won || _since > probeInterval) {
        if (stepSwitch())
            _since = 0;
        else
            rotateStep();
    }
    return _current;
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
