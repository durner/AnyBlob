#pragma once
#include "network/config.hpp"
#include <chrono>
#include <cstdint>
#include <optional>
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
class TaskedSendReceiverGroup;
//---------------------------------------------------------------------------
/// Adaptively saturate throughput using requests before threads
/// Each epoch judges the step of the previous epoch against the measured throughput: a step that
/// pays is repeated, a step that does not is rolled back and hands the turn to the next one, so
/// the controller climbs while it wins and probes the other knobs once it holds
class AdaptiveController {
    public:
    /// Epoch length for statistics collection
    static constexpr std::chrono::milliseconds epochLength{1000};
    /// Throughput increase (distinguish noise)
    static constexpr double tolerance = 0.05;
    /// Fraction of throughput a growth step needs
    static constexpr double efficiency = 0.2;
    /// Number of epochs between probes
    static constexpr unsigned probeInterval = 5;
    /// Epochs of memory in the plateau estimate
    static constexpr unsigned smoothing = 4;
    /// The advisory output
    struct Recommendation {
        /// Threads
        unsigned threads;
        /// Requests per thread
        unsigned requestsPerThread;

        /// Comparison
        bool operator==(const Recommendation& other) const = default;
    };

    /// The epoch measurement sample
    struct Sample {
        /// Bytes finished during the epoch
        uint64_t transferredBytes;
        /// Wall time of the epoch
        std::chrono::nanoseconds elapsed;
        /// Queued submissions at end
        uint64_t queuedMessages;
        /// Messages in flight at end
        uint64_t inflightMessages;
        /// The configuration that actually ran
        Recommendation applied;
        /// The hard request bound during the epoch
        unsigned maxRequestsPerThread;
    };

    /// Constructor
    explicit AdaptiveController(const Config& config, bool tls, unsigned hardwareThreads = 0);

    /// Recommnedation tick, the threads are the ones the caller actually runs
    [[nodiscard]] Recommendation recommend(const TaskedSendReceiverGroup& group, unsigned runningThreads);
    /// Get the current recommendation
    [[nodiscard]] Recommendation current() const { return _current; }
    /// Get the maximum number of threads
    [[nodiscard]] unsigned maxThreads() const { return _maxThreads; }

    /// Measure and react
    Recommendation measure(const Sample& sample);

    private:
    /// What the controller tries next
    enum class Step : uint8_t {
        /// Raise the requests per thread
        RequestsUp,
        /// Add threads
        ThreadsUp,
        /// Give threads back
        ThreadsDown,
        /// Carry the same concurrency on fewer threads
        Repack,
        /// Lower the requests per thread
        RequestsDown,
    };

    /// Epoch information
    struct Epoch {
        /// Start of the epoch
        std::chrono::steady_clock::time_point start;
        /// Transferred bytes at epoch start
        uint64_t transferredBytes;
    };

    /// The threads maximum
    unsigned _maxThreads;
    /// The current recommendation
    Recommendation _current;
    /// The last kept configuration
    Recommendation _previous;
    /// The configuration of the previous sample
    Recommendation _applied;
    /// Smoothed throughput of the kept configuration
    double _plateauThroughput = 0;
    /// Throughput of the previous epoch
    double _lastThroughput = 0;
    /// The step of the next epoch
    Step _next = Step::RequestsUp;
    /// Epochs since the last step
    unsigned _since = probeInterval;
    /// Whether the threads are parked
    bool _parked = false;
    /// The epoch baseline
    std::optional<Epoch> _epoch;
};
//---------------------------------------------------------------------------
} // namespace anyblob::network
