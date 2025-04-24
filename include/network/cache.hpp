#pragma once
#include "network/tls_connection.hpp"
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <netdb.h>
#include <netinet/tcp.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
//---------------------------------------------------------------------------
namespace network {
//---------------------------------------------------------------------------
/// The addr resolver and cacher, which is not thread safe
class Cache {
    public:
    /// The dns entry
    struct DnsEntry {
        /// The addr
        std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addr;
        /// The cache priority
        int cachePriority = 0;

        /// The constructor
        DnsEntry(std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> address, int cachePriority = 0) : addr(move(address)), cachePriority(cachePriority) {}
    };

    /// The fd socket entry
    struct SocketEntry {
        /// The DnsEntry
        std::unique_ptr<DnsEntry> dns;
        /// The optional tls connection
        std::unique_ptr<TLSConnection> tls;
        /// The timestamp
        size_t timestamp;
        /// The fd
        int32_t fd;
        /// The port
        unsigned port;
        /// The hostname
        std::string hostname;

        /// The constructor
        SocketEntry(std::string hostname, unsigned port);
    };

    protected:
    /// The cache, uses hostname as key, multimap traversals same keys in the insertion order
    std::multimap<std::string, std::unique_ptr<SocketEntry>> _cache;
    /// The fifo deletion of sockets to help reduce open fds (ulimit -n issue)
    std::map<size_t, SocketEntry*> _fifo;
    /// The timestamp counter for deletion
    size_t _timestamp = 0;
    /// The default priority
    int _defaultPriority = 8;

    public:
    /// The address resolving
    virtual std::unique_ptr<SocketEntry> resolve(const std::string& hostname, unsigned port, bool tls);
    /// Start the timing and advance to the next cache bucket
    virtual void startSocket(int /*fd*/) {}
    /// Stops the socket and either closes the connection or cashes it
    virtual void stopSocket(std::unique_ptr<SocketEntry> socketEntry, uint64_t bytes, unsigned cachedEntries, bool reuseSocket);
    /// Shutdown of the socket and clears the same addresses
    virtual void shutdownSocket(std::unique_ptr<SocketEntry> socketEntry, unsigned cachedEntries);
    /// The destructor
    virtual ~Cache();

    /// Get the tld
    static std::string_view tld(std::string_view domain);
};
//---------------------------------------------------------------------------
}; // namespace network
//---------------------------------------------------------------------------
}; // namespace anyblob
