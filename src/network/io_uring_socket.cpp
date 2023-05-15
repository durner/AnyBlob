#include "network/io_uring_socket.hpp"
#include <cstring>
#include <limits>
#include <stdexcept>
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
IOUringSocket::IOUringSocket(uint32_t entries, int32_t /*flags*/) : _resolverCache()
// Constructor that inits uring queue
{
    // initialize io_uring
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));

    if (io_uring_queue_init_params(entries, &_uring, &params) < 0) {
        throw runtime_error("Uring init error!");
    }

    if (!(params.features & IORING_FEAT_FAST_POLL)) {
        throw runtime_error("Uring init error - IORING_FEAT_FAST_POLL not available in the kernel!");
    }

    _eventId = eventfd(0, 0);
    if (_eventId < 0)
        throw runtime_error("Event FD creation error!");
    io_uring_register_eventfd(&_uring, _eventId);
    _resolverCache.emplace("", make_unique<ThroughputResolver>(entries));
}
//---------------------------------------------------------------------------
int32_t IOUringSocket::connect(string hostname, uint32_t port, TCPSettings& tcpSettings, int retryLimit)
// Creates a new socket connection
{
    if (auto it = _fdCache.find(hostname); it != _fdCache.end()) {
        auto fd = it->second;
        _fdCache.erase(it);
        _fdSockets.insert(fd);
        return fd;
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
    auto pos = resCache->resolve(hostname, port_str, oldAddress);

    // Build socket
    auto fd = socket(resCache->_addr[pos]->ai_family, resCache->_addr[pos]->ai_socktype, resCache->_addr[pos]->ai_protocol);
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

    auto setTimeOut = [](int fd, TCPSettings& tcpSettings) {
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
    auto connectRes = ::connect(fd, resCache->_addr[pos]->ai_addr, resCache->_addr[pos]->ai_addrlen);
    if (connectRes < 0 && errno != EINPROGRESS) {
        close(fd);
        if (oldAddress && retryLimit > 0) {
            // Retry with a new address
            resCache->erase();
            return connect(hostname, port, tcpSettings, retryLimit - 1);
        } else {
            throw runtime_error("Socket creation error! " + string(strerror(errno)));
        }
    }

    resCache->startSocket(fd, pos);
    resCache->increment();

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
                _fdSockets.insert(fd);
                return fd;
            } else {
                close(fd);
                throw runtime_error("Socket creation error! " + string(strerror(socketError)));
            }
        } else {
            // Reached timeout
            close(fd);
            if (retryLimit > 0) {
                return connect(hostname, port, tcpSettings, retryLimit - 1);
            } else {
                throw runtime_error("Socket creation error! Timeout reached");
            }
        }
    }
    setTimeOut(fd, tcpSettings);
    _fdSockets.insert(fd);
    return fd;
}
//---------------------------------------------------------------------------
void IOUringSocket::disconnect(int32_t fd, string hostname, uint32_t port, TCPSettings* tcpSettings, uint64_t bytes, bool forceShutdown)
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
    if (forceShutdown)
        shutdown(fd, SHUT_RDWR);
    if (tcpSettings && tcpSettings->reuse > 0 && hostname.length() > 0 && port) {
        _fdCache.emplace(hostname, fd);
        _fdSockets.erase(fd);
    } else {
        _fdSockets.erase(fd);
        close(fd);
    }
}
//---------------------------------------------------------------------------
void IOUringSocket::addResolver(const string& hostname, unique_ptr<Resolver> resolver)
// Add resolver
{
    _resolverCache.emplace(string(Resolver::tld(hostname)), move(resolver));
}
//---------------------------------------------------------------------------
io_uring_sqe* IOUringSocket::send_prep(const Request* req, int32_t msg_flags, int32_t flags)
// Prepare a submission (sqe) send
{
    auto sqe = io_uring_get_sqe(&_uring);
    io_uring_prep_send(sqe, req->fd, req->data.cdata, req->length, msg_flags);
    sqe->flags |= flags;
    sqe->user_data = reinterpret_cast<uintptr_t>(req);
    return sqe;
}
//---------------------------------------------------------------------------
io_uring_sqe* IOUringSocket::recv_prep(Request* req, int32_t msg_flags, int32_t flags)
// Prepare a submission (sqe) recv
{
    auto sqe = io_uring_get_sqe(&_uring);
    io_uring_prep_recv(sqe, req->fd, req->data.data, req->length, msg_flags);
    sqe->flags |= flags;
    sqe->user_data = reinterpret_cast<uintptr_t>(req);
    return sqe;
}
//---------------------------------------------------------------------------
IOUringSocket::Request* IOUringSocket::peek()
// Get a completion (cqe) event if it is available and mark it as seen; return the SQE attached Request if available else nullptr
{
    io_uring_cqe* cqe;
    auto res = io_uring_peek_cqe(&_uring, &cqe);
    if (!res) {
        Request* req = nullptr;
        memcpy(&req, &cqe->user_data, sizeof(cqe->user_data));
        req->length = cqe->res;
        seen(cqe);
        return req;
    }
    return nullptr;
}
//---------------------------------------------------------------------------
IOUringSocket::Request* IOUringSocket::complete()
// Get a completion (cqe) event and mark it as seen; return the SQE attached Request
{
    io_uring_cqe* cqe;
    auto res = io_uring_wait_cqe(&_uring, &cqe);
    if (!res) {
        Request* req = nullptr;
        memcpy(&req, &cqe->user_data, sizeof(cqe->user_data));
        req->length = cqe->res;
        seen(cqe);
        return req;
    }
    throw runtime_error("io_uring_wait_cqe error!");
}
//---------------------------------------------------------------------------
io_uring_cqe* IOUringSocket::completion()
// Get a completion (cqe) event
{
    io_uring_cqe* cqe;
    auto res = io_uring_wait_cqe(&_uring, &cqe);
    if (!res) {
        return cqe;
    }
    throw runtime_error("Completion error!");
}
//---------------------------------------------------------------------------
uint32_t IOUringSocket::submitCompleteAll(uint32_t events, vector<IOUringSocket::Request*>& completions)
// Submits queue and gets all completion (cqe) event and mark them as seen; return the SQE attached requests
{
    uint32_t count = 0;
    while (events > count) {
        io_uring_submit_and_wait(&_uring, 1);
        io_uring_cqe* cqe;
        uint32_t head;
        uint32_t localCount = 0;

        // iterate all cqes
        io_uring_for_each_cqe(&_uring, head, cqe) {
            localCount++;
            Request* req = nullptr;
            memcpy(&req, &cqe->user_data, sizeof(cqe->user_data));
            req->length = cqe->res;
            completions.push_back(req);
        }
        io_uring_cq_advance(&_uring, localCount);
        count += localCount;
    }
    return count;
}
//---------------------------------------------------------------------------
void IOUringSocket::seen(io_uring_cqe* cqe)
// Mark a completion (cqe) event seen to allow for new completions in the kernel
{
    io_uring_cqe_seen(&_uring, cqe);
}
//---------------------------------------------------------------------------
int32_t IOUringSocket::submit()
// Submit uring to the kernel and return the number of submitted entries
{
    return io_uring_submit(&_uring);
}
//---------------------------------------------------------------------------
void IOUringSocket::wait()
// Waits for an cqe event in the uring
{
    eventfd_t v;
    int ret = eventfd_read(_eventId, &v);
    if (ret < 0)
        throw runtime_error("Wait error!");
}
//---------------------------------------------------------------------------
bool IOUringSocket::checkTimeout(int fd, TCPSettings& tcpSettings)
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
IOUringSocket::~IOUringSocket()
// The destructor
{
    io_uring_queue_exit(&_uring);
    close(_eventId);
    for (auto f : _fdSockets)
        close(f);
    for (auto& f : _fdCache)
        close(f.second);
}
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
