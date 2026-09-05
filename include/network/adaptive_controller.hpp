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
class AdaptiveController {
    public:
    /// Epoch length for statistics collection
    static constexpr std::chrono::milliseconds epochLength{1000};
    /// Throughput increase (distinguish noise)
    static constexpr double tolerance = 0.05;
    /// Fraction of throughput a growth step needs
    static constexpr double efficiency = 0.5;
    /// Number of epochs between probes
    static constexpr unsigned probeInterval = 10;
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
    };

    /// A zero seed derives the initial thread count from hardware, a zero bound detects it
    explicit AdaptiveController(unsigned maxRequestsPerThread, unsigned seedThreads = 0, unsigned hardwareThreads = 0);

    /// Estimate initial threads from instance bandwidth and protocol
    [[nodiscard]] static unsigned seedThreads(const Config& config, bool tls, unsigned hardwareThreads);

    /// Recommnedation tick
    [[nodiscard]] Recommendation recommend(const TaskedSendReceiverGroup& group);
    /// Get the current recommendation
    [[nodiscard]] Recommendation current() const { return _current; }
    /// Get the maximum number of threads
    [[nodiscard]] unsigned maxThreads() const { return _bounds.maxThreads; }

    /// Measure and react
    Recommendation measure(const Sample& sample);

    private:
    /// The control phases
    enum class Phase : uint8_t {
        Grow, // We want to increase throughput
        Hold, // We want to hold throughput
        Idle
    };

    /// What the last move changed
    enum class Move : uint8_t {
        None, // We hold
        RequestsUp, // We increased requests
        RequestsDown, // We decreased requests
        ThreadsUp, // We added threads
        ThreadsDown, // We removed threads
        Repacked // We repacked threads and requests
    };

    /// The rotating probe steps
    enum class Probe : uint8_t {
        /// Give a thread back
        DecreaseThreads,
        /// Move the requests in their learned direction
        MoveRequests,
        /// Carry the same concurrency on fewer threads
        Repack,
        /// Add a thread
        GrowThreads,
    };

    /// Bounds for the adaptive controller
    struct Bounds {
        /// The upper bound for the requests
        unsigned maxRequests;
        /// The upper bound for the threads
        unsigned maxThreads;
    };

    /// Throughput statistics
    struct Statistics {
        /// Best throughput
        double bestThroughput = 0;
        /// Throughput of the previous epoch
        double lastThroughput = 0;
        /// What the last move changed
        Move lastMove = Move::None;
    };

    /// The probe schedule
    struct Schedule {
        /// Epochs since the last probe
        unsigned since = 0;
        /// The rotating probe slot
        Probe probe = Probe::DecreaseThreads;
        /// Whether the requests still pay upwards
        bool requestsUp = true;
        /// Whether threads can still be traded for requests
        bool repack = true;
    };

    /// Epoch information
    struct Epoch {
        /// Start of the epoch
        std::chrono::steady_clock::time_point start;
        /// Transferred bytes at epoch start
        uint64_t transferredBytes;
    };

    /// The static bounds
    Bounds _bounds;
    /// The current recommendation
    Recommendation _current;
    /// The last kept configuration
    Recommendation _previous;
    /// The throughput memory
    Statistics _statistics;
    /// The control phase
    Phase _phase = Phase::Grow;
    /// The probe schedule
    Schedule _schedule;
    /// The epoch baseline
    std::optional<Epoch> _epoch;
};
//---------------------------------------------------------------------------
} // namespace anyblob::network
