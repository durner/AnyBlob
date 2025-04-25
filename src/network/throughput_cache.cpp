#ifdef ANYBLOB_LIBCXX_COMPAT
#error "You must not include throughput_cache.cpp when building with libc++"
#endif
#include "network/throughput_cache.hpp"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <arpa/inet.h>
#include <sys/types.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace network {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
ThroughputCache::ThroughputCache() : Cache(), _throughputTree(), _throughput(), _throughputIterator()
// Constructor
{
    _defaultPriority = 2;
    _throughput.resize(_maxHistory);
}
//---------------------------------------------------------------------------
void ThroughputCache::startSocket(int fd)
// Start a socket
{
    _fdMap.emplace(fd, chrono::steady_clock::now());
}
//---------------------------------------------------------------------------
void ThroughputCache::stopSocket(std::unique_ptr<SocketEntry> socketEntry, uint64_t bytes, unsigned cachedEntries, bool reuseSocket)
// Stop a socket
{
    auto now = chrono::steady_clock::now();
    auto it = _fdMap.find(socketEntry->fd);
    if (it != _fdMap.end()) {
        auto timeNs = chrono::duration_cast<chrono::nanoseconds>(now - it->second).count();
        auto throughput = static_cast<double>(bytes) / chrono::duration<double>(timeNs).count();
        auto curThroughput = _throughputIterator++;
        auto del = _throughput[curThroughput % _maxHistory];
        _throughput[curThroughput % _maxHistory] = throughput;
        _throughputTree.erase(del);
        _throughputTree.insert(throughput);
        auto maxElem = _throughputIterator < _maxHistory ? _throughputIterator : _maxHistory;
        bool possible = true;
        if (maxElem > 3) {
            auto percentile = _throughputTree.find_by_order(maxElem / 3);
            if (percentile.m_p_nd->m_value <= throughput)
                socketEntry->dns->cachePriority += 1;
            else
                possible = false;
        }
        if (possible && maxElem > 6) {
            auto percentile = _throughputTree.find_by_order(maxElem / 6);
            if (percentile.m_p_nd->m_value <= throughput)
                socketEntry->dns->cachePriority += 2;
        }
    }
    Cache::stopSocket(move(socketEntry), bytes, cachedEntries, reuseSocket);
}
//---------------------------------------------------------------------------
}; // namespace network
//---------------------------------------------------------------------------
}; // namespace anyblob
