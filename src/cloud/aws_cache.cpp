#include "cloud/aws_cache.hpp"
#include <array>
#include <charconv>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <arpa/inet.h>
#include <sys/types.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::cloud {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
AWSCache::AWSCache() : Cache(), _mtuCache()
// Constructor
{
}
//---------------------------------------------------------------------------
unique_ptr<network::Cache::SocketEntry> AWSCache::resolve(const string& hostname, unsigned port, bool tls)
// Resolve the request
{
    for (auto it = _cache.find(hostname); it != _cache.end();) {
        // loosely clean up the multimap cache
        if (!it->second->dns) {
            _fifo.erase(it->second->timestamp);
            it = _cache.erase(it);
            continue;
        }
        if (it->second->port == port && ((tls && it->second->tls.get()) || (!tls && !it->second->tls.get()))) {
            auto socketEntry = move(it->second);
            socketEntry->dns->cachePriority--;
            _fifo.erase(socketEntry->timestamp);
            _cache.erase(it);
            return socketEntry;
        }
        it++;
    }
    struct addrinfo hints = {};
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* temp;
    array<char, 16> port_str{};
    to_chars(port_str.data(), port_str.data() + port_str.size(), port);
    if (getaddrinfo(hostname.c_str(), port_str.data(), &hints, &temp) != 0) {
        throw runtime_error("hostname getaddrinfo error");
    }
    auto socketEntry = make_unique<Cache::SocketEntry>(hostname, port);
    socketEntry->dns = make_unique<DnsEntry>(unique_ptr<addrinfo, decltype(&freeaddrinfo)>(temp, &freeaddrinfo), _defaultPriority);

    if (!Cache::tld(hostname).compare("amazonaws.com")) {
        auto* p = reinterpret_cast<sockaddr_in*>(socketEntry->dns->addr->ai_addr);
        auto ipAsInt = p->sin_addr.s_addr;
        auto it = _mtuCache.find(ipAsInt);
        if (it != _mtuCache.end()) {
            if (it->second)
                socketEntry->dns->cachePriority = numeric_limits<int>::max();
        } else {
            array<char, INET_ADDRSTRLEN> ipv4{};
            inet_ntop(AF_INET, &p->sin_addr, ipv4.data(), INET_ADDRSTRLEN);
            string cmd = "timeout 0.01 ping -s 1473 -D ";
            cmd += string_view(ipv4.data(), strnlen(ipv4.data(), ipv4.size()));
            cmd += " -c 1 >>/dev/null 2>>/dev/null";
            auto res = system(cmd.c_str());
            if (!res) {
                _mtuCache.emplace(ipAsInt, true);
                socketEntry->dns->cachePriority = numeric_limits<int>::max();
            } else {
                _mtuCache.emplace(ipAsInt, false);
            }
        }
    }
    return socketEntry;
}
//---------------------------------------------------------------------------
} // namespace anyblob::cloud
//---------------------------------------------------------------------------
