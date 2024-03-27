#ifndef ANYBLOB_LIBCXX_COMPAT 
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
ThroughputCache::ThroughputCache(unsigned entries) : Cache(entries), _throughputTree(), _throughput(), _throughputIterator()
// Constructor
{
    _throughput.resize(_maxHistory);
}
//---------------------------------------------------------------------------
const addrinfo* ThroughputCache::resolve(string hostname, string port, bool& reuse)
// Resolve the request
{
    auto addrPos = _addrCtr % _addrString.size();
    auto curCtr = _addrString[addrPos].second--;
    auto hostString = hostname + ":" + port;
    if (_addrString[addrPos].first.compare(hostString) || curCtr == 0) {
        struct addrinfo hints = {};
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* temp;
        if (getaddrinfo(hostname.c_str(), port.c_str(), &hints, &temp)) {
            throw runtime_error("hostname getaddrinfo error");
        }
        _addr[addrPos].reset(temp);
        _addrString[addrPos] = {hostString, 2};
        reuse = false;
    }
    reuse = true;
    return _addr[addrPos].get();
}
//---------------------------------------------------------------------------
void ThroughputCache::startSocket(int fd)
// Start a socket
{
    _fdMap.emplace(fd, make_pair(_addrCtr++ % _addrString.size(), chrono::steady_clock::now()));
}
//---------------------------------------------------------------------------
void ThroughputCache::shutdownSocket(int fd)
// Shutdown a socket and clear the dns cache
{
    auto it = _fdMap.find(fd);
    if (it != _fdMap.end()) {
        auto pos = it->second.first;
        auto& ip = _addr[pos];
        for (auto compPos = 0u; compPos < _addr.size(); compPos++) {
            if (_addr[compPos] && !strncmp(ip->ai_addr->sa_data, _addr[compPos]->ai_addr->sa_data, 14)) {
                _addrString[compPos].second = 0;
            }
        }
    }
}
//---------------------------------------------------------------------------
void ThroughputCache::stopSocket(int fd, uint64_t bytes)
// Stop a socket
{
    auto now = chrono::steady_clock::now();
    auto it = _fdMap.find(fd);
    if (it != _fdMap.end()) {
        auto timeNs = chrono::duration_cast<chrono::nanoseconds>(now - it->second.second).count();
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
                _addrString[it->second.first].second += 1;
            else
                possible = false;
        }
        if (possible && maxElem > 6) {
            auto percentile = _throughputTree.find_by_order(maxElem / 6);
            if (percentile.m_p_nd->m_value <= throughput)
                _addrString[it->second.first].second += 2;
        }
    }
}
//---------------------------------------------------------------------------
}; // namespace network
//---------------------------------------------------------------------------
}; // namespace anyblob
#endif // ANYBLOB_LIBCXX_COMPAT
