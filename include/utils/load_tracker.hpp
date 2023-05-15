#pragma once
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <sys/times.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//---------------------------------------------------------------------------
// Adopted from http://blog.davidecoppola.com/2016/12/cpp-program-to-get-cpu-usage-from-command-line-in-linux/
//---------------------------------------------------------------------------
// https://github.com/vivaladav/cpu-stat
// MIT License
// Copyright (c) 2016 Davide Coppola
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//---------------------------------------------------------------------------
namespace anyblob {
namespace utils {
class LoadTracker {
    public:
    /// Load values for each CPU
    struct Values {
        /// The active time
        double activeTimeAllProcesses;
        /// The idle time
        double idleTimeAllProcesses;
        /// The cpu name
        std::string cpuName;
        /// The user time for the process
        double userTime;
        /// The sys time for the process
        double sysTime;
        /// The total time for the process
        double elapsedTime;
        /// The constructor
        Values() : activeTimeAllProcesses(), idleTimeAllProcesses(), cpuName(), userTime(), sysTime(), elapsedTime() {}
    };

    private:
    /// States amount
    static constexpr int NUM_CPU_STATES = 10;
    /// CPU statues
    enum class CPUStates : uint8_t {
        S_USER,
        S_NICE,
        S_SYSTEM,
        S_IDLE,
        S_IOWAIT,
        S_IRQ,
        S_SOFTIRQ,
        S_STEAL,
        S_GUEST,
        S_GUEST_NICE
    };

    /// CPU data strucht
    struct CPUData {
        std::string cpu;
        size_t times[NUM_CPU_STATES];
        clock_t elapsedTime, sysTime, userTime;
    };

    /// The outstream
    std::vector<std::unique_ptr<Values>>& _loadValues;
    /// Start entries of the cpu data
    std::vector<CPUData> _startEntries;
    /// Only cpu total
    bool _totalOnly;

    public:
    /// Constructor
    LoadTracker(std::vector<std::unique_ptr<Values>>& loadValues, bool totalOnly = true) : _loadValues(loadValues), _totalOnly(totalOnly) {
        readStatsCPU(_startEntries);
    }

    /// Destructor
    ~LoadTracker() {
        std::vector<CPUData> endEntries;
        readStatsCPU(endEntries);
        writeStats(_startEntries, endEntries);
    }

    private:
    /// Get the idle times where other work could run
    static constexpr size_t getIdleTime(const CPUData& e) {
        return e.times[static_cast<uint8_t>(CPUStates::S_IDLE)] + e.times[static_cast<uint8_t>(CPUStates::S_IOWAIT)];
    }

    /// Get the active cpu times
    static constexpr size_t getActiveTime(const CPUData& e) {
        return e.times[static_cast<uint8_t>(CPUStates::S_USER)] + e.times[static_cast<uint8_t>(CPUStates::S_NICE)] +
            e.times[static_cast<uint8_t>(CPUStates::S_SYSTEM)] + e.times[static_cast<uint8_t>(CPUStates::S_IRQ)] +
            e.times[static_cast<uint8_t>(CPUStates::S_SOFTIRQ)] + e.times[static_cast<uint8_t>(CPUStates::S_STEAL)];
    }

    /// Write stats as csv
    inline void writeStats(const std::vector<CPUData>& entries1, const std::vector<CPUData>& entries2) {
        const size_t NUM_ENTRIES = entries1.size();

        for (size_t i = 0; i < NUM_ENTRIES; ++i) {
            const CPUData& e1 = entries1[i];
            const CPUData& e2 = entries2[i];

            if (_totalOnly && e1.cpu.compare("tot")) {
                continue;
            }

            if (_loadValues.size() < i)
                throw std::runtime_error("Out of bound: loadValues");

            if (_loadValues[i]->cpuName.size() && _loadValues[i]->cpuName != e1.cpu)
                throw std::runtime_error("Wrong index: loadValues");
            else if (!_loadValues[i]->cpuName.size())
                _loadValues[i]->cpuName = e1.cpu;

            _loadValues[i]->activeTimeAllProcesses += static_cast<double>(getActiveTime(e2) - getActiveTime(e1));
            _loadValues[i]->idleTimeAllProcesses += static_cast<double>(getIdleTime(e2) - getIdleTime(e1));

            if (!e1.cpu.compare("tot")) {
                _loadValues[i]->sysTime += static_cast<double>(e2.sysTime - e1.sysTime);
                _loadValues[i]->userTime += static_cast<double>(e2.userTime - e1.userTime);
                _loadValues[i]->elapsedTime += static_cast<double>(e2.elapsedTime - e1.elapsedTime);
            }
        }
    }

    /// Read the stats
    static void readStatsCPU(std::vector<CPUData>& entries) {
        std::ifstream fileStat("/proc/stat");
        std::string line;
        const std::string STR_CPU("cpu");
        const std::size_t LEN_STR_CPU = STR_CPU.size();
        while (std::getline(fileStat, line)) {
            // cpu stats line found
            if (!line.compare(0, LEN_STR_CPU, STR_CPU)) {
                std::istringstream ss(line);

                // store entry
                entries.emplace_back(CPUData());
                CPUData& entry = entries.back();

                // read cpu label
                ss >> entry.cpu;

                // remove "cpu" from the label when it's a processor number
                if (entry.cpu.size() > LEN_STR_CPU)
                    entry.cpu.erase(0, LEN_STR_CPU);
                // replace "cpu" with "tot" when it's total values
                else
                    entry.cpu = "tot";

                // read times
                for (int i = 0; i < NUM_CPU_STATES; ++i)
                    ss >> entry.times[i];
            }
        }
        for (auto& e : entries) {
            if (!e.cpu.compare("tot")) {
                tms timeSample;
                e.elapsedTime = times(&timeSample);
                e.sysTime = timeSample.tms_stime;
                e.userTime = timeSample.tms_utime;
            }
        }
    }
};
//---------------------------------------------------------------------------
}; // namespace utils
}; // namespace anyblob
