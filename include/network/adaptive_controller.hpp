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
/// Advisory controller that recommends threads and requests per thread from measured throughput
/// Without a bandwidth target the measured plateau becomes the set point
class AdaptiveController {
    public:
    /// Control epoch length
    static constexpr std::chrono::milliseconds epochLength{1000};
    /// Throughput band that separates change from noise
    static constexpr double tolerance = 0.05;
    /// At most doubling per climbing step
    static constexpr double growthFactor = 2.0;
    /// Epochs between probes while holding
    static constexpr unsigned probeInterval = 10;

    /// The advisory output
    struct Recommendation {
        /// Threads the caller should run
        unsigned threads;
        /// Requests per thread the caller should apply to the group
        unsigned requestsPerThread;

        /// Comparison
        bool operator==(const Recommendation& other) const = default;
    };

    /// One epoch measurement
    struct Sample {
        /// Bytes finished during the epoch
        uint64_t transferredBytes;
        /// Wall time of the epoch
        std::chrono::nanoseconds elapsed;
        /// Queued submissions at epoch end
        uint64_t queuedMessages;
        /// Messages in flight at epoch end
        uint64_t inflightMessages;
    };

    /// Constructor that seeds from the config
    explicit AdaptiveController(const Config& config, unsigned maxRequestsPerThread, double attainment = 0.98);

    /// Caller driven tick with one control decision per epoch
    [[nodiscard]] Recommendation recommend(const TaskedSendReceiverGroup& group);
    /// Get the current recommendation without measuring
    [[nodiscard]] Recommendation current() const { return _current; }
    /// Get the maximum number of threads the controller recommends
    [[nodiscard]] unsigned maxThreads() const { return _bounds.maxThreads; }

    /// The deterministic control law for one epoch sample
    Recommendation update(const Sample& sample);

    private:
    /// The control phase
    enum class Phase : uint8_t {
        Grow,
        Hold,
        Idle
    };

    /// Static bounds from the constructor
    struct Bounds {
        /// Target bytes per second, zero seeks the plateau
        double targetBytesPerSec;
        /// Target fraction that counts as attained
        double attainment;
        /// The upper bound for the requests knob
        unsigned maxRequests;
        /// The upper bound for the threads knob
        unsigned maxThreads;
    };

    /// Throughput memory for judging steps
    struct Trend {
        /// Best proven throughput
        double plateauBps = 0;
        /// Throughput of the previous epoch
        double lastBps = 0;
        /// Whether the last step grew
        bool stepUp = false;
    };

    /// The probe schedule while holding
    struct Probe {
        /// Epochs since the last probe
        unsigned since = 0;
        /// Direction of the next probe
        bool up = true;
    };

    /// Baseline of the current epoch
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
    Trend _trend;
    /// The control phase
    Phase _phase = Phase::Grow;
    /// The probe schedule
    Probe _probe;
    /// The epoch baseline, empty until the first tick
    std::optional<Epoch> _epoch;
};
//---------------------------------------------------------------------------
} // namespace anyblob::network
