#include "network/connection_manager.hpp"
#include "network/cache.hpp"
#ifdef ANYBLOB_HAS_IO_URING
#include "network/io_uring_socket.hpp"
#endif
#include "network/poll_socket.hpp"
#include "network/tls_connection.hpp"
#include "network/tls_context.hpp"
#ifndef ANYBLOB_LIBCXX_COMPAT
#include "network/throughput_cache.hpp"
#endif
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2024
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network {
//---------------------------------------------------------------------------
using namespace std;
std::atomic<unsigned> ConnectionManager::_activeConnectionManagers{0};
//---------------------------------------------------------------------------
ConnectionManager::ConnectionManager([[maybe_unused]] unsigned uringEntries) : _cache()
// The constructor
{
#ifdef ANYBLOB_HAS_IO_URING
    // Init can fail on runtime instances with kernel < 5.6 as we do not have the uring syscall or specific feature is not available
    try {
        _socketWrapper = make_unique<IOUringSocket>(uringEntries);
    } catch (std::runtime_error& /*error*/) {
        // Fall back to poll socket
        _socketWrapper = make_unique<PollSocket>();
    }
#else
    _socketWrapper = make_unique<PollSocket>();
#endif
    _activeConnectionManagers++;
    _context = make_unique<network::TLSContext>();
#ifndef ANYBLOB_LIBCXX_COMPAT
    // By default, use the ThroughputCache.
    _cache.emplace("", make_unique<ThroughputCache>());
#else
    // If we build in libcxx compatability mode, we need to use the default cache.
    // The ThroughputCache currently does not build with libcxx.
    _cache.emplace("", make_unique<Cache>());
#endif
}
//---------------------------------------------------------------------------
int32_t ConnectionManager::connect(const string& hostname, uint32_t port, bool tls, const TCPSettings& tcpSettings, int retryLimit)
// Creates a new socket connection
{
    Cache* resCache;
    auto tldName = string(Cache::tld(hostname));
    auto it = _cache.find(tldName);
    if (it != _cache.end()) {
        resCache = it->second.get();
    } else {
        resCache = _cache.find("")->second.get();
    }

    // Did we use
    auto socketEntry = resCache->resolve(hostname, port, tls);

    auto fd = socketEntry->fd;
    if (fd >= 0) {
        _fdSockets.emplace(socketEntry->fd, move(socketEntry));
        return fd;
    }

    // Build socket
    socketEntry->fd = socket(socketEntry->dns->addr->ai_family, socketEntry->dns->addr->ai_socktype, socketEntry->dns->addr->ai_protocol);
    if (socketEntry->fd == -1) {
        throw runtime_error("Socket creation error!" + string(strerror(errno)));
    }

    auto maxCacheEntries = (_maxCachedFds / _activeConnectionManagers) + 1;
    // Settings for socket
    // No blocking mode
    if (tcpSettings.nonBlocking > 0) {
        int flags = fcntl(socketEntry->fd, F_GETFL, 0);
        flags |= O_NONBLOCK;
        if (fcntl(socketEntry->fd, F_SETFL, flags) < 0) {
            resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
            throw runtime_error("Socket creation error! - non blocking error");
        }
    }

    // Keep Alive
    if (tcpSettings.keepAlive > 0) {
        if (setsockopt(socketEntry->fd, SOL_SOCKET, SO_KEEPALIVE, &tcpSettings.keepAlive, sizeof(tcpSettings.keepAlive))) {
            resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
            throw runtime_error("Socket creation error! - keep alive error");
        }
    }

    // Keep Idle
    if (tcpSettings.keepIdle > 0) {
        if (setsockopt(socketEntry->fd, SOL_TCP, TCP_KEEPIDLE, &tcpSettings.keepIdle, sizeof(tcpSettings.keepIdle))) {
            resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
            throw runtime_error("Socket creation error! - keep idle error");
        }
    }

    // Keep intvl
    if (tcpSettings.keepIntvl > 0) {
        if (setsockopt(socketEntry->fd, SOL_TCP, TCP_KEEPINTVL, &tcpSettings.keepIntvl, sizeof(tcpSettings.keepIntvl))) {
            resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
            throw runtime_error("Socket creation error! - keep intvl error");
        }
    }

    // Keep cnt
    if (tcpSettings.keepCnt > 0) {
        if (setsockopt(socketEntry->fd, SOL_TCP, TCP_KEEPCNT, &tcpSettings.keepCnt, sizeof(tcpSettings.keepCnt))) {
            resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
            throw runtime_error("Socket creation error! - keep cnt error");
        }
    }

    // No Delay
    if (tcpSettings.noDelay > 0) {
        if (setsockopt(socketEntry->fd, SOL_TCP, TCP_NODELAY, &tcpSettings.noDelay, sizeof(tcpSettings.noDelay))) {
            resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
            throw runtime_error("Socket creation error! - nodelay error");
        }
    }

    // Reuse ports
    if (tcpSettings.reusePorts > 0) {
        if (setsockopt(socketEntry->fd, SOL_SOCKET, SO_REUSEPORT, &tcpSettings.reusePorts, sizeof(tcpSettings.reusePorts))) {
            resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
            throw runtime_error("Socket creation error! - reuse port error");
        }
    }

    // Recv buffer
    if (tcpSettings.recvBuffer > 0) {
        if (setsockopt(socketEntry->fd, SOL_SOCKET, SO_RCVBUF, &tcpSettings.recvBuffer, sizeof(tcpSettings.recvBuffer))) {
            resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
            throw runtime_error("Socket creation error! - recvbuf error");
        }
    }

    // Lingering of sockets
    if (tcpSettings.linger > 0) {
        if (setsockopt(socketEntry->fd, SOL_TCP, TCP_LINGER2, &tcpSettings.linger, sizeof(tcpSettings.linger))) {
            resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
            throw runtime_error("Socket creation error - linger timeout error!" + string(strerror(errno)));
        }
    }

    // TCP window shift
#ifdef TCP_WINSHIFT
    if (tcpSettings.windowShift && tcpSettings.recvBuffer >= (64 << 10)) {
        int windowShift = 0;
        for (int s = tcpSettings.recvBuffer >> 16; s > 0; s >>= 1) {
            windowShift++;
        }

        if (setsockopt(socketEntry->fd, SOL_TCP, TCP_WINSHIFT, &windowShift, sizeof(windowShift))) {
            resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
            throw runtime_error("Socket creation error - win shift error!");
        }
    }
#endif

    // RFC 1323 optimization
#ifdef TCP_RFC1323
    if (tcpSettings.rfc1323 && tcpSettings.recvBuffer >= (64 << 10)) {
        int on = 1;
        if (setsockopt(socketEntry->fd, SOL_TCP, TCP_RFC1323, &on, sizeof(on))) {
            resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
            throw runtime_error("Socket creation error - rfc 1323 error!");
        }
    }
#endif

    // Max segment size
#ifdef TCP_MAXSEG
    if (tcpSettings.mss > 0) {
        if (setsockopt(socketEntry->fd, SOL_TCP, TCP_MAXSEG, &tcpSettings.mss, sizeof(tcpSettings.mss))) {
            resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
            throw runtime_error("Socket creation error - max segment error!");
        }
    }
#endif

    auto setTimeOut = [&resCache, &socketEntry, maxCacheEntries](int fd, const TCPSettings& tcpSettings) {
        // Set timeout
        if (tcpSettings.timeoutValue > 0) {
            struct timeval tv;
            tv.tv_sec = tcpSettings.timeoutValue / (1000 * 1000);
            tv.tv_usec = tcpSettings.timeoutValue % (1000 * 1000);
            if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof tv)) {
                resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
                throw runtime_error("Socket creation error - recv timeout error!");
            }
            if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof tv)) {
                resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
                throw runtime_error("Socket creation error - send timeout error!");
            }
            int timeoutInMs = tcpSettings.timeoutValue / 1000;
            if (setsockopt(fd, SOL_TCP, TCP_USER_TIMEOUT, &timeoutInMs, sizeof(timeoutInMs))) {
                resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
                throw runtime_error("Socket creation error - tcp timeout error!" + string(strerror(errno)));
            }
        }
    };

    // Connect to remote
    auto connectRes = ::connect(socketEntry->fd, socketEntry->dns->addr->ai_addr, socketEntry->dns->addr->ai_addrlen);
    if (connectRes < 0 && errno != EINPROGRESS) {
        resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
        if (retryLimit > 0) {
            return connect(hostname, port, tls, tcpSettings, retryLimit - 1);
        } else {
            throw runtime_error("Socket creation error! " + string(strerror(errno)));
        }
    }

    resCache->startSocket(socketEntry->fd);

    auto emplaceSocket = [this, &socketEntry, tls]() {
        auto fd = socketEntry->fd;
        if (tls)
            socketEntry->tls = make_unique<TLSConnection>(*_context);
        _fdSockets.emplace(fd, move(socketEntry));
        return fd;
    };

    if (connectRes < 0 && errno == EINPROGRESS) {
        // connection check
        struct pollfd pollEvent;
        pollEvent.fd = socketEntry->fd;
        pollEvent.events = POLLIN | POLLOUT;

        // connection check
        auto t = poll(&pollEvent, 1, tcpSettings.timeoutValue / 1000);
        if (t == 1) {
            int socketError;
            socklen_t socketErrorLen = sizeof(socketError);
            if (getsockopt(socketEntry->fd, SOL_SOCKET, SO_ERROR, &socketError, &socketErrorLen)) {
                resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
                throw runtime_error("Socket creation error! Could not retrieve socket options!");
            }

            if (!socketError) {
                // sucessful
                setTimeOut(socketEntry->fd, tcpSettings);
                return emplaceSocket();
            } else {
                resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
                throw runtime_error("Socket creation error! " + string(strerror(socketError)));
            }
        } else {
            // Reached timeout
            resCache->shutdownSocket(move(socketEntry), maxCacheEntries);
            if (retryLimit > 0) {
                return connect(hostname, port, tls, tcpSettings, retryLimit - 1);
            } else {
                throw runtime_error("Socket creation error! Timeout reached");
            }
        }
    }
    setTimeOut(socketEntry->fd, tcpSettings);
    return emplaceSocket();
}
//---------------------------------------------------------------------------
void ConnectionManager::disconnect(int32_t fd, const TCPSettings* tcpSettings, uint64_t bytes, bool forceShutdown)
// Disconnects the socket
{
    auto socketIt = _fdSockets.find(fd);
    assert(socketIt != _fdSockets.end());
    Cache* resCache;
    auto tldName = string(Cache::tld(socketIt->second->hostname));
    auto it = _cache.find(tldName);
    if (it != _cache.end()) {
        resCache = it->second.get();
    } else {
        resCache = _cache.find("")->second.get();
    }
    auto maxCacheEntries = (_maxCachedFds / _activeConnectionManagers) + 1;
    if (forceShutdown) {
        resCache->shutdownSocket(move(socketIt->second), maxCacheEntries);
        _fdSockets.erase(socketIt);
    } else {
        resCache->stopSocket(move(socketIt->second), bytes, maxCacheEntries, tcpSettings ? tcpSettings->reuse : false);
        _fdSockets.erase(socketIt);
    }
}
//---------------------------------------------------------------------------
void ConnectionManager::addCache(const string& hostname, unique_ptr<Cache> cache)
// Add cache
{
    _cache.emplace(string(Cache::tld(hostname)), move(cache));
}
//---------------------------------------------------------------------------
bool ConnectionManager::checkTimeout(int fd, const TCPSettings& tcpSettings)
// Check for a timeout
{
    pollfd p = {fd, POLLIN, 0};
    int r = poll(&p, 1, tcpSettings.timeoutValue / 1000);
    if (r == 0) {
        shutdown(fd, SHUT_RDWR);
        return true;
    }
    return false;
}
//---------------------------------------------------------------------------
TLSConnection* ConnectionManager::getTLSConnection(int32_t fd)
// Get the tls connection of the fd
{
    auto it = _fdSockets.find(fd);
    assert(it != _fdSockets.end());
    return it->second->tls.get();
}
//---------------------------------------------------------------------------
ConnectionManager::~ConnectionManager()
// The destructor
{
    for (auto& f : _fdSockets)
        close(f.first);

    _activeConnectionManagers--;
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
