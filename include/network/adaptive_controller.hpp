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
/// Saturate throughput by increasing requests before threads.
/// Repeat successful steps; revert failures and try the next adjustment.
/// Probe periodically once settled.
class AdaptiveController {
    public:
    /// Measurement interval
    static constexpr std::chrono::milliseconds epochLength{2000};
    /// Throughput tolerance for noise
    static constexpr double tolerance = 0.05;
    /// Number of epochs between probes
    static constexpr unsigned probeInterval = 5;
    /// Recommended concurrency
    struct Recommendation {
        /// Threads
        unsigned threads;
        /// Requests per thread
        unsigned requestsPerThread;

        /// Comparison
        bool operator==(const Recommendation& other) const = default;
    };

    /// Measurements for one epoch
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
        /// Request limit per thread during the epoch
        unsigned maxRequestsPerThread;
    };

    /// Constructor
    explicit AdaptiveController(const Config& config, bool tls, unsigned hardwareThreads = 0);

    /// Update using the actual running thread count
    [[nodiscard]] Recommendation recommend(const TaskedSendReceiverGroup& group, unsigned runningThreads);
    /// Get the current recommendation
    [[nodiscard]] Recommendation current() const { return _current; }
    /// Get the maximum number of threads
    [[nodiscard]] unsigned maxThreads() const { return _maxThreads; }

    /// Measure and react
    Recommendation measure(const Sample& sample);

    private:
    /// Next adjustment
    enum class Step : uint8_t {
        /// Raise the requests per thread
        RequestsUp,
        /// Add threads
        ThreadsUp,
        /// Remove threads and redistribute their requests
        ThreadsDown,
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

    /// Thread limit
    unsigned _maxThreads;
    /// The current recommendation
    Recommendation _current;
    /// The last kept configuration
    Recommendation _kept;
    /// Last throughput of the kept configuration
    double _keptThroughput = 0;
    /// The step on trial
    Step _step = Step::RequestsUp;
    /// Epochs measured with the current recommendation
    unsigned _since = 0;
    /// Whether the threads are parked
    bool _parked = false;
    /// The epoch baseline
    std::optional<Epoch> _epoch;
};
//---------------------------------------------------------------------------
} // namespace anyblob::network
