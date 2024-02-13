#include "utils/timer.hpp"
#include <iostream>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace utils {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
Timer& Timer::operator=(Timer&& rhs)
// Starts timer for step s
{
    for (auto it : _totalTimer) {
        auto res = rhs._totalTimer.find(it.first);
        if (res != rhs._totalTimer.end()) {
            it.second += res->second;
            rhs._totalTimer.erase(res);
        }
    }
    for (auto it : rhs._totalTimer) {
        _totalTimer.insert(it);
    }
    return *this;
}
//---------------------------------------------------------------------------
void Timer::start(Steps s)
// Starts timer for step s
{
    _currentTimer[s] = chrono::steady_clock::now();
    if (_totalLoad.find(s) == _totalLoad.end()) {
        auto vec = make_unique<vector<unique_ptr<LoadTracker::Values>>>();
        vec->emplace_back(make_unique<LoadTracker::Values>());
        _totalLoad.insert({s, move(vec)});
    }
    _loadTracker[s] = make_unique<LoadTracker>(*_totalLoad.find(s)->second.get());
}
//---------------------------------------------------------------------------
void Timer::stop(Steps s)
// Stops timer for step s
{
    _loadTracker[s].reset();
    auto time = chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - _currentTimer[s]).count();
    _totalTimer[s] += time;
}
//---------------------------------------------------------------------------
void Timer::printResult(ostream& s)
// Print results
{
    // The reverse steps
    string reverseSteps[] = {"Overall", "Upload", "Download", "Submit", "Request", "Response", "Buffer", "BufferCapacity", "BufferResize", "Finishing", "Waiting", "Queue", "SocketCreation", "BufferCapacitySend"};

    if (_writeHeader)
        s << "Step,Pct,Time,CPUUserTime,CPUSysTime,CPUElapsedTime,CPUActiveAllProcesses,CPUIdleAllProcesses" << _headerInfo << endl;
    uint64_t total = 0;
    for (auto it : _totalTimer) {
        total += it.second;
    }
    for (auto it : _totalTimer) {
        s << reverseSteps[it.first] << "," << static_cast<double>(it.second) / static_cast<double>(total) << "," << static_cast<double>(it.second) / (1000 * 1000);
        auto elem = _totalLoad.find(it.first);
        if (elem != _totalLoad.end())
            s << "," << elem->second->at(0)->userTime << "," << elem->second->at(0)->sysTime << "," << elem->second->at(0)->elapsedTime << "," << elem->second->at(0)->activeTimeAllProcesses << "," << elem->second->at(0)->idleTimeAllProcesses;
        else
            s << ",-1,-1,-1,-1,-1";
        s << _contentInfo << endl;
    }
}
//---------------------------------------------------------------------------
void Timer::setInfo(string headerInfo, string contentInfo)
// Sets the info
{
    this->_headerInfo = headerInfo;
    this->_contentInfo = contentInfo;
}
//---------------------------------------------------------------------------
}; // namespace utils
}; // namespace anyblob
