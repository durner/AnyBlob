#include "network/cache.hpp"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
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
string_view Cache::tld(string_view domain)
// String prefix
{
    auto pos = domain.find_last_of('.');
    if (pos != string::npos) {
        pos = domain.substr(0, pos - 1).find_last_of('.');
        if (pos == string::npos)
            return domain;
        else
            return string_view(domain).substr(pos + 1, domain.size());
    }
    return string_view();
}
//---------------------------------------------------------------------------
Cache::SocketEntry::SocketEntry(string hostname, unsigned port) : dns(nullptr), tls(nullptr), fd(-1), port(port), hostname(move(hostname))
// The constructor
{}
//---------------------------------------------------------------------------
void Cache::shutdownSocket(unique_ptr<Cache::SocketEntry> socketEntry, unsigned cacheEntries)
// Shutdown the socket and dns cache
{
    // delete all occurences of the cached ips in the cache map
    if (socketEntry->hostname.length() > 0) {
        for (auto it = _cache.find(socketEntry->hostname); it != _cache.end();) {
            if (!strncmp(socketEntry->dns->addr->ai_addr->sa_data, it->second->dns->addr->ai_addr->sa_data, 14)) {
                it->second->dns->cachePriority = 0;
                _fifo.erase(it->second->timestamp);
                stopSocket(move(it->second), 0, cacheEntries, false);
                it = _cache.erase(it);
            } else {
                it++;
            }
        }
    }
    stopSocket(move(socketEntry), 0, cacheEntries, false);
}
//---------------------------------------------------------------------------
void Cache::stopSocket(unique_ptr<Cache::SocketEntry> socketEntry, uint64_t /*bytes*/, unsigned cacheEntries, bool reuseSocket)
// Stops the socket and either closes the connection or cashes it
{
    if (reuseSocket && socketEntry->hostname.length() > 0 && socketEntry->port) {
        if (socketEntry->dns->cachePriority > 0) {
            socketEntry->timestamp = _timestamp++;
            for (auto it = _fifo.begin(); _fifo.size() >= cacheEntries;) {
                if (it->second->fd >= 0)
                    close(it->second->fd);
                it->second->fd = -1;
                it->second->dns = nullptr;
                it = _fifo.erase(it);
            }
            _fifo.emplace(socketEntry->timestamp, socketEntry.get());
            _cache.emplace(socketEntry->hostname, move(socketEntry));
        } else {
            close(socketEntry->fd);
        }
    } else {
        if (socketEntry->fd >= 0) {
            close(socketEntry->fd);
        }
        socketEntry->fd = -1;
        if (socketEntry->dns->cachePriority > 0)
            _cache.emplace(socketEntry->hostname, move(socketEntry));
    }
}
//---------------------------------------------------------------------------
unique_ptr<Cache::SocketEntry> Cache::resolve(string hostname, unsigned port, bool tls)
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
    char port_str[16] = {};
    sprintf(port_str, "%d", port);
    if (getaddrinfo(hostname.c_str(), port_str, &hints, &temp) != 0) {
        throw runtime_error("hostname getaddrinfo error");
    }
    auto socketEntry = make_unique<Cache::SocketEntry>(hostname, port);
    socketEntry->dns = make_unique<DnsEntry>(unique_ptr<addrinfo, decltype(&freeaddrinfo)>(temp, &freeaddrinfo), _defaultPriority);
    return socketEntry;
}
//---------------------------------------------------------------------------
Cache::~Cache()
// The destructor
{
    for (auto& f : _cache)
        if (f.second->fd >= 0)
            close(f.second->fd);
}
//---------------------------------------------------------------------------
}; // namespace network
//---------------------------------------------------------------------------
}; // namespace anyblob
