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
    _previous = _current;
    _applied = _current;
}
//---------------------------------------------------------------------------
AdaptiveController::Recommendation AdaptiveController::recommend(const TaskedSendReceiverGroup& group, unsigned runningThreads)
/// Recommnedation tick
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
    _previous.requestsPerThread = min(_previous.requestsPerThread, maxRequests);
    _applied.requestsPerThread = min(_applied.requestsPerThread, maxRequests);
    auto seconds = chrono::duration<double>(sample.elapsed).count();
    auto throughput = seconds > 0 ? static_cast<double>(sample.transferredBytes) / seconds : 0.0;
    auto applied = sample.applied;
    auto concurrency = static_cast<uint64_t>(applied.threads) * applied.requestsPerThread;
    // Require settled throughput before adding concurrency
    auto settled = _lastThroughput > 0 && abs(throughput - _lastThroughput) <= tolerance * _lastThroughput;

    /// Whether a step adds concurrency
    auto raisesConcurrency = [](Step step) {
        return step == Step::RequestsUp || step == Step::ThreadsUp;
    };

    /// The step the epoch ran
    auto pendingStep = [&]() -> optional<Step> {
        if (applied.threads != _previous.threads)
            return applied.threads > _previous.threads ? Step::ThreadsUp : Step::ThreadsDown;
        if (applied.requestsPerThread != _previous.requestsPerThread)
            return applied.requestsPerThread > _previous.requestsPerThread ? Step::RequestsUp : Step::RequestsDown;
        return {};
    };

    /// Whether the step was beneficial
    auto wasBeneficial = [&](Step step) {
        if (step == Step::RequestsUp || step == Step::RequestsDown)
            return throughput >= (1 + tolerance) * _lastThroughput;
        // Thread growth must pay out
        if (step == Step::ThreadsUp) {
            auto previous = static_cast<double>(_previous.threads) * _previous.requestsPerThread;
            auto band = max(tolerance, efficiency * abs((static_cast<double>(concurrency) - previous) / previous));
            return throughput >= (1 + band) * _lastThroughput;
        }
        // A thread costs a core, so rather release
        return throughput >= (1 - tolerance) * _plateauThroughput;
    };

    /// Try out a different step
    auto rotateStep = [&]() {
        _next = static_cast<Step>((static_cast<uint8_t>(_next) + 1) % (static_cast<uint8_t>(Step::RequestsDown) + 1));
    };

    /// Move the knob of the next step
    auto stepSwitch = [&]() {
        constexpr auto stepFraction = 4;
        switch (_next) {
            case Step::RequestsUp: _current.requestsPerThread = increase(_current.requestsPerThread, stepFraction >> 1, maxRequests); break;
            case Step::ThreadsUp: _current.threads = increase(_current.threads, stepFraction, _maxThreads); break;
            case Step::ThreadsDown: _current.threads = decrease(_current.threads, stepFraction, 1); break;
            case Step::Repack: {
                // Carry the concurrency that ran on fewer threads, at the granularity of the other steps
                auto packed = decrease(_current.threads, stepFraction, 1);
                auto requests = static_cast<unsigned>((concurrency + packed - 1) / packed);
                if (packed < _current.threads && requests <= maxRequests) {
                    _current.threads = packed;
                    _current.requestsPerThread = requests;
                }
                break;
            }
            case Step::RequestsDown: _current.requestsPerThread = decrease(_current.requestsPerThread, stepFraction, 1); break;
        }
        return _current != _previous;
    };

    // No demand -> reduce the threads
    if (!sample.transferredBytes && !sample.queuedMessages && !sample.inflightMessages) {
        if (!_parked) {
            _current = _previous;
            _current.threads = 1;
            _parked = true;
        }
        _applied = applied;
        return _current;
    }
    // Reuse best configuration
    if (_parked) {
        _current = _previous;
        _applied = applied;
        _parked = false;
        _since = 0;
        _lastThroughput = throughput;
        return _current;
    }

    // A configuration should only be measured when it actually ran
    if (applied != _applied) {
        _applied = applied;
        return _current;
    }

    auto step = pendingStep();
    if (step && !wasBeneficial(*step)) {
        _current = _previous;
        _since = 0;
        rotateStep();
    } else {
        if (step) {
            // Keep moving while the step pays
            _previous = applied;
            _since = probeInterval;
        }
        _plateauThroughput = _plateauThroughput > 0 ? _plateauThroughput + (throughput - _plateauThroughput) / smoothing : throughput;
        _current = applied;
        auto due = _since++ >= probeInterval && (settled || !raisesConcurrency(_next));
        if (due && !stepSwitch())
            rotateStep();
    }
    _lastThroughput = throughput;
    return _current;
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
