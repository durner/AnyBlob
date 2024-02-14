#pragma once
#include "network/resolver.hpp"
#include <cassert>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <liburing.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2024
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace network {
//---------------------------------------------------------------------------
class TLSConnection;
class TLSContext;
//---------------------------------------------------------------------------
// This class acts as the connection enabler, closer, and main
// cache for sockets and their optional tls connection.
// We further add the DNS resolution to the connection manager.
class ConnectionManager {
    public:
    /// The tcp settings
    struct TCPSettings {
        /// flag for nonBlocking
        int nonBlocking = 1;
        /// flag for noDelay
        int noDelay = 0;
        /// flag for recv no wait
        int recvNoWait = 0;
        /// flag for keepAlive
        int keepAlive = 1;
        /// time for tcp keepIdle
        int keepIdle = 1;
        /// time for tcp keepIntvl
        int keepIntvl = 1;
        /// probe count
        int keepCnt = 1;
        /// recv buffer for tcp
        int recvBuffer = 0;
        /// Maximum segment size
        int mss = 0;
        /// Reuse port
        int reusePorts = 0;
        /// Lingering of tcp packets
        int linger = 1;
        /// The timeout in usec
        int timeout = 500 * 1000;
        /// Reuse sockets
        int reuse = 0;
        /// The kernel timeout parameter
        __kernel_timespec kernelTimeout;

        TCPSettings() {
            kernelTimeout.tv_sec = 0;
            kernelTimeout.tv_nsec = timeout * 1000;
        }
    };

    /// The fd socket entry
    struct SocketEntry {
        /// The optional tls connection
        std::unique_ptr<TLSConnection> tls;
        /// The fd
        int32_t fd;
        /// The port
        unsigned port;
        /// The hostname
        std::string hostname;

        SocketEntry(int32_t fd, std::string hostname, unsigned port, std::unique_ptr<TLSConnection> tls);
    };

    private:
    /// The socket wrapper
    std::unique_ptr<IOUringSocket> _socketWrapper;
    /// The active sockets
    std::unordered_map<int32_t, std::unique_ptr<SocketEntry>> _fdSockets;
    /// The fd socket cache, uses hostname as key
    std::unordered_multimap<std::string, std::unique_ptr<SocketEntry>> _fdCache;
    /// Resolver
    std::unordered_map<std::string, std::unique_ptr<Resolver>> _resolverCache;
    /// The tls context
    std::unique_ptr<network::TLSContext> _context;

    public:
    /// The constructor
    explicit ConnectionManager(unsigned uringEntries, unsigned resolverCacheEntries);
    /// The destructor
    ~ConnectionManager();

    /// Creates a new socket connection
    [[nodiscard]] int32_t connect(std::string hostname, uint32_t port, bool tls, const TCPSettings& tcpSettings, bool useCache = true, int retryLimit = 16);
    /// Disconnects the socket
    void disconnect(int32_t fd, std::string hostname = "", uint32_t port = 0, const TCPSettings* tcpSettings = nullptr, uint64_t bytes = 0, bool forceShutdown = false);

    /// Add resolver
    void addResolver(const std::string& hostname, std::unique_ptr<Resolver> resolver);
    /// Checks for a timeout
    bool checkTimeout(int fd, const TCPSettings& settings);

    /// Get the socket
    IOUringSocket& getSocketConnection() {
        assert(_socketWrapper);
        return *_socketWrapper.get();
    }

    /// Get the tls connection of the fd
    TLSConnection* getTLSConnection(int32_t fd);
};
//---------------------------------------------------------------------------
}; // namespace network
}; // namespace anyblob
