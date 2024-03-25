#include "network/connection_manager.hpp"
#include "network/io_uring_socket.hpp"
#include "network/tls_connection.hpp"
#include "network/tls_context.hpp"
#ifndef ANYBLOB_LIBCXX_COMPAT
#include "network/throughput_resolver.hpp"
#endif
#include <cassert>
#include <cstring>
#include <string>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/eventfd.h>
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
namespace anyblob {
namespace network {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
ConnectionManager::SocketEntry::SocketEntry(int32_t fd, std::string hostname, unsigned port, std::unique_ptr<TLSConnection> tls) : tls(move(tls)), fd(fd), port(port), hostname(move(hostname))
// The constructor
{}
//---------------------------------------------------------------------------
ConnectionManager::ConnectionManager(unsigned uringEntries, unsigned resolverCacheEntries) : _socketWrapper(make_unique<IOUringSocket>(uringEntries)), _resolverCache()
// The constructor
{
    _context = make_unique<network::TLSContext>();
#ifndef ANYBLOB_LIBCXX_COMPAT
    // By default, use the ThroughputResolver.
    _resolverCache.emplace("", make_unique<ThroughputResolver>(resolverCacheEntries));
#else
    // If we build in libcxx compatability mode, we need to use the default resolver.
    // The ThroughputResolver currently does not build with libcxx.
    _resolverCache.emplace("", make_unique<Resolver>(resolverCacheEntries));
#endif
}
//---------------------------------------------------------------------------
int32_t ConnectionManager::connect(string hostname, uint32_t port, bool tls, const TCPSettings& tcpSettings, bool useCache, int retryLimit)
// Creates a new socket connection
{
    if (useCache) {
        for (auto it = _fdCache.find(hostname); it != _fdCache.end(); it++) {
            if (it->second->port == port && ((tls && it->second->tls.get()) || (!tls && !it->second->tls.get()))) {
                auto fd = it->second->fd;
                _fdSockets.emplace(fd, move(it->second));
                _fdCache.erase(it);
                return fd;
            }
        }
    }

    char port_str[16] = {};
    sprintf(port_str, "%d", port);

    Resolver* resCache;
    auto tldName = string(Resolver::tld(hostname));
    auto it = _resolverCache.find(tldName);
    if (it != _resolverCache.end()) {
        resCache = it->second.get();
    } else {
        resCache = _resolverCache.find("")->second.get();
    }

    // Did we use
    bool oldAddress = false;
    const auto* addr = resCache->resolve(hostname, port_str, oldAddress);

    // Build socket
    auto fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (fd == -1) {
        throw runtime_error("Socket creation error!" + string(strerror(errno)));
    }

    // Settings for socket
    // No blocking mode
    if (tcpSettings.nonBlocking > 0) {
        int flags = fcntl(fd, F_GETFL, 0);
        flags |= O_NONBLOCK;
        if (fcntl(fd, F_SETFL, flags) < 0) {
            throw runtime_error("Socket creation error! - non blocking error");
        }
    }

    // Keep Alive
    if (tcpSettings.keepAlive > 0) {
        if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &tcpSettings.keepAlive, sizeof(tcpSettings.keepAlive))) {
            throw runtime_error("Socket creation error! - keep alive error");
        }
    }

    // Keep Idle
    if (tcpSettings.keepIdle > 0) {
        if (setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &tcpSettings.keepIdle, sizeof(tcpSettings.keepIdle))) {
            throw runtime_error("Socket creation error! - keep idle error");
        }
    }

    // Keep intvl
    if (tcpSettings.keepIntvl > 0) {
        if (setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &tcpSettings.keepIntvl, sizeof(tcpSettings.keepIntvl))) {
            throw runtime_error("Socket creation error! - keep intvl error");
        }
    }

    // Keep cnt
    if (tcpSettings.keepCnt > 0) {
        if (setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &tcpSettings.keepCnt, sizeof(tcpSettings.keepCnt))) {
            throw runtime_error("Socket creation error! - keep cnt error");
        }
    }

    // No Delay
    if (tcpSettings.noDelay > 0) {
        if (setsockopt(fd, SOL_TCP, TCP_NODELAY, &tcpSettings.noDelay, sizeof(tcpSettings.noDelay))) {
            throw runtime_error("Socket creation error! - nodelay error");
        }
    }

    // Reuse ports
    if (tcpSettings.reusePorts > 0) {
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &tcpSettings.reusePorts, sizeof(tcpSettings.reusePorts))) {
            throw runtime_error("Socket creation error! - reuse port error");
        }
    }

    // Recv buffer
    if (tcpSettings.recvBuffer > 0) {
        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &tcpSettings.recvBuffer, sizeof(tcpSettings.recvBuffer))) {
            throw runtime_error("Socket creation error! - recvbuf error");
        }
    }

    // Lingering of sockets
    if (tcpSettings.linger > 0) {
        if (setsockopt(fd, SOL_TCP, TCP_LINGER2, &tcpSettings.linger, sizeof(tcpSettings.linger))) {
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

        if (setsockopt(inSock, SOL_TCP, TCP_WINSHIFT, &windowShift, sizeof(windowShift))) {
            throw runtime_error("Socket creation error - win shift error!");
        }
    }
#endif

    // RFC 1323 optimization
#ifdef TCP_RFC1323
    if (tcpSettings.rfc1323 && tcpSettings.recvBuffer >= (64 << 10)) {
        int on = 1;
        if (setsockopt(fd, SOL_TCP, TCP_RFC1323, &on, sizeof(on))) {
            throw runtime_error("Socket creation error - rfc 1323 error!");
        }
    }
#endif

    // Max segment size
#ifdef TCP_MAXSEG
    if (tcpSettings.mss > 0) {
        if (setsockopt(fd, SOL_TCP, TCP_MAXSEG, &tcpSettings.mss, sizeof(tcpSettings.mss))) {
            throw runtime_error("Socket creation error - max segment error!");
        }
    }
#endif

    auto setTimeOut = [](int fd, const TCPSettings& tcpSettings) {
        // Set timeout
        if (tcpSettings.timeout > 0) {
            struct timeval tv;
            tv.tv_sec = tcpSettings.timeout / (1000 * 1000);
            tv.tv_usec = tcpSettings.timeout % (1000 * 1000);
            if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof tv)) {
                throw runtime_error("Socket creation error - recv timeout error!");
            }
            if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof tv)) {
                throw runtime_error("Socket creation error - send timeout error!");
            }
            int timeoutInMs = tcpSettings.timeout / 1000;
            if (setsockopt(fd, SOL_TCP, TCP_USER_TIMEOUT, &timeoutInMs, sizeof(timeoutInMs))) {
                throw runtime_error("Socket creation error - tcp timeout error!" + string(strerror(errno)));
            }
        }
    };

    // Connect to remote
    auto connectRes = ::connect(fd, addr->ai_addr, addr->ai_addrlen);
    if (connectRes < 0 && errno != EINPROGRESS) {
        close(fd);
        if (oldAddress && retryLimit > 0) {
            // Retry with a new address
            resCache->resetBucket();
            return connect(hostname, port, tls, tcpSettings, useCache, retryLimit - 1);
        } else {
            throw runtime_error("Socket creation error! " + string(strerror(errno)));
        }
    }

    resCache->startSocket(fd);

    auto emplaceSocket = [this, &hostname, port, tls](int32_t fd) {
        if (tls)
            _fdSockets.emplace(fd, make_unique<SocketEntry>(fd, hostname, port, make_unique<TLSConnection>(*_context)));
        else
            _fdSockets.emplace(fd, make_unique<SocketEntry>(fd, hostname, port, nullptr));
    };

    if (connectRes < 0 && errno == EINPROGRESS) {
        // connection check
        struct pollfd pollEvent;
        pollEvent.fd = fd;
        pollEvent.events = POLLIN | POLLOUT;

        // connection check
        auto t = poll(&pollEvent, 1, tcpSettings.timeout / 1000);
        if (t == 1) {
            int socketError;
            socklen_t socketErrorLen = sizeof(socketError);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &socketErrorLen)) {
                throw runtime_error("Socket creation error! Could not retrieve socket options!");
            }

            if (!socketError) {
                // sucessful
                setTimeOut(fd, tcpSettings);
                emplaceSocket(fd);
                return fd;
            } else {
                close(fd);
                throw runtime_error("Socket creation error! " + string(strerror(socketError)));
            }
        } else {
            // Reached timeout
            close(fd);
            if (retryLimit > 0) {
                return connect(hostname, port, tls, tcpSettings, useCache, retryLimit - 1);
            } else {
                throw runtime_error("Socket creation error! Timeout reached");
            }
        }
    }
    setTimeOut(fd, tcpSettings);
    emplaceSocket(fd);
    return fd;
}
//---------------------------------------------------------------------------
void ConnectionManager::disconnect(int32_t fd, string hostname, uint32_t port, const TCPSettings* tcpSettings, uint64_t bytes, bool forceShutdown)
// Disconnects the socket
{
    Resolver* resCache;
    auto tldName = string(Resolver::tld(hostname));
    auto it = _resolverCache.find(tldName);
    if (it != _resolverCache.end()) {
        resCache = it->second.get();
    } else {
        resCache = _resolverCache.find("")->second.get();
    }
    resCache->stopSocket(fd, bytes);
    if (forceShutdown) {
        shutdown(fd, SHUT_RDWR);
        resCache->shutdownSocket(fd);
        _fdSockets.erase(fd);
    } else if (tcpSettings && tcpSettings->reuse > 0 && hostname.length() > 0 && port) {
        auto it = _fdSockets.find(fd);
        assert(it != _fdSockets.end());
        _fdCache.emplace(hostname, move(it->second));
        _fdSockets.erase(it);
    } else {
        _fdSockets.erase(fd);
        close(fd);
    }
}
//---------------------------------------------------------------------------
void ConnectionManager::addResolver(const string& hostname, unique_ptr<Resolver> resolver)
// Add resolver
{
    _resolverCache.emplace(string(Resolver::tld(hostname)), move(resolver));
}
//---------------------------------------------------------------------------
bool ConnectionManager::checkTimeout(int fd, const TCPSettings& tcpSettings)
// Check for a timeout
{
    pollfd p = {fd, POLLIN, 0};
    int r = poll(&p, 1, tcpSettings.timeout / 1000);
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
    for (auto& f : _fdCache)
        close(f.second->fd);
}
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
