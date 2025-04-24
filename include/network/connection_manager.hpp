#pragma once
#include "network/cache.hpp"
#include <cassert>
#include <memory>
#include <string>
#include <unordered_map>
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
class Socket;
class TLSContext;
struct DnsEntry;
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
        int reuse = 1;
        /// The kernel timeout parameter
        __kernel_timespec kernelTimeout;

        TCPSettings() {
            kernelTimeout.tv_sec = 0;
            kernelTimeout.tv_nsec = timeout * 1000;
        }
    };

    private:
    /// The socket wrapper
    std::unique_ptr<Socket> _socketWrapper;
    /// The active sockets
    std::unordered_map<int32_t, std::unique_ptr<Cache::SocketEntry>> _fdSockets;
    /// Cache
    std::unordered_map<std::string, std::unique_ptr<Cache>> _cache;
    /// The tls context
    std::unique_ptr<network::TLSContext> _context;

    /// The counter of current connection managers
    static std::atomic<unsigned> _activeConnectionManagers;
    /// The maximum number of cached fds, TODO: access ulimit for this
    constexpr static unsigned _maxCachedFds = 512;

    public:
    /// The constructor
    explicit ConnectionManager(unsigned uringEntries);
    /// The destructor
    ~ConnectionManager();

    /// Creates a new socket connection
    [[nodiscard]] int32_t connect(std::string hostname, uint32_t port, bool tls, const TCPSettings& tcpSettings, int retryLimit = 0);
    /// Disconnects the socket
    void disconnect(int32_t fd, const TCPSettings* tcpSettings = nullptr, uint64_t bytes = 0, bool forceShutdown = false);

    /// Add domain-specific cache
    void addCache(const std::string& hostname, std::unique_ptr<Cache> cache);
    /// Checks for a timeout
    bool checkTimeout(int fd, const TCPSettings& settings);

    /// Get the socket
    Socket& getSocketConnection() {
        assert(_socketWrapper);
        return *_socketWrapper.get();
    }

    /// Get the tls connection of the fd
    TLSConnection* getTLSConnection(int32_t fd);
};
//---------------------------------------------------------------------------
}; // namespace network
}; // namespace anyblob
