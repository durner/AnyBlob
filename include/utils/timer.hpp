#pragma once
#include "utils/load_tracker.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <unordered_map>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::utils {
//---------------------------------------------------------------------------
/// This is a timing helper
struct TimingHelper {
    /// The file size
    uint64_t size;
    /// The start time
    std::chrono::steady_clock::time_point start;
    /// The first recieve time
    std::chrono::steady_clock::time_point recieve;
    /// The finish time
    std::chrono::steady_clock::time_point finish;
};
//---------------------------------------------------------------------------
class Timer {
    public:
    enum Steps : uint64_t {
        Overall,
        Upload,
        Download,
        Submit,
        Request,
        Response,
        Buffer,
        BufferCapacity,
        BufferResize,
        Finishing,
        Waiting,
        Queue,
        SocketCreation,
        BufferCapacitySend
    };

    /// Timer Guard
    class TimerGuard {
        /// The guarded step
        Steps step;
        /// The current timer
        Timer* timer;

        public:
        /// Constructor
        TimerGuard(Steps step, Timer* timer) : step(step), timer(timer) {
            if (timer)
                timer->start(step);
        }
        /// Destructor
        ~TimerGuard() {
            if (timer)
                timer->stop(step);
        }
    };

    private:
    /// Currently active timer
    std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> _currentTimer;
    /// The load tracker
    std::unordered_map<uint64_t, std::unique_ptr<LoadTracker>> _loadTracker;
    /// Total time in ns
    std::unordered_map<uint64_t, uint64_t> _totalTimer;
    /// Total time in ns
    std::unordered_map<uint64_t, std::unique_ptr<std::vector<std::unique_ptr<LoadTracker::Values>>>> _totalLoad;
    /// The outstream
    std::ostream* _outStream;
    /// Is the header printed
    bool _writeHeader;
    /// Header info
    std::string _headerInfo;
    /// Content info
    std::string _contentInfo;
    /// Vector of timer values
    std::vector<std::pair<std::chrono::steady_clock::time_point, std::chrono::steady_clock::time_point>> timings;

    public:
    /// Default constructor
    Timer() = default;
    /// Constructor
    Timer(std::ostream* outStream, bool writeHeader = true) : _outStream(outStream), _writeHeader(writeHeader) {}
    /// Default copy constructor
    Timer(const Timer&) = default;
    /// Move Assignment - Timer and Merge
    Timer& operator=(Timer&& rhs) noexcept;
    /// Destructor
    ~Timer() {
        if (_outStream)
            printResult(*_outStream);
    }
    /// Sets the ostream
    void setOutStream(std::ostream* outStream, bool writeHeader = true) {
        _outStream = outStream;
        _writeHeader = writeHeader;
    }
    /// Reserve timings
    void reserveTimings(uint64_t cap) {
        timings.reserve(cap);
    }
    /// Set info settings
    void setInfo(std::string headerInfo, std::string contentInfo);
    /// Starts the timer for step s
    void start(Steps s);
    /// Stop the timer and adds to totalTimer for step s
    void stop(Steps s);
    /// Prints the result
    void printResult(std::ostream& s);
};
//---------------------------------------------------------------------------
} // namespace anyblob::utils
