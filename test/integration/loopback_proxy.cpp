#include "loopback_proxy.hpp"
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2026
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::test {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
LoopbackProxy::LoopbackProxy(const string& target, const char* name)
// Resolve the target and listen on a free loopback port
{
    auto colon = target.rfind(':');
    if (colon == string::npos)
        throw runtime_error(string(name) + " proxy needs a host:port target");
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* resolved = nullptr;
    if (getaddrinfo(target.substr(0, colon).c_str(), target.substr(colon + 1).c_str(), &hints, &resolved))
        throw runtime_error(string(name) + " proxy cannot resolve " + target);
    memcpy(&_target, resolved->ai_addr, sizeof(_target));
    freeaddrinfo(resolved);

    _listenFd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t addressLength = sizeof(address);
    auto listening = _listenFd >= 0;
    listening = listening && !::bind(_listenFd, reinterpret_cast<sockaddr*>(&address), addressLength);
    listening = listening && !::listen(_listenFd, 64);
    listening = listening && !getsockname(_listenFd, reinterpret_cast<sockaddr*>(&address), &addressLength);
    if (!listening) {
        if (_listenFd >= 0)
            ::close(_listenFd);
        throw runtime_error(string(name) + " proxy cannot listen on loopback");
    }
    bound(_listenFd);
    _port = ntohs(address.sin_port);
}
//---------------------------------------------------------------------------
LoopbackProxy::~LoopbackProxy()
// Close the listening socket, the derived destructor stops the threads
{
    ::close(_listenFd);
}
//---------------------------------------------------------------------------
void LoopbackProxy::start(function<void(int, uint64_t)> relay)
// Hand every accepted connection and its index to the relay until the proxy stops
{
    _thread = thread([this, relay = move(relay)]() {
        while (!_stop) {
            auto client = ::accept(_listenFd, nullptr, nullptr);
            if (client < 0)
                continue;
            auto index = _accepted.fetch_add(1, memory_order_relaxed);
            _relays.emplace_back([relay, client, index]() { relay(client, index); });
        }
    });
}
//---------------------------------------------------------------------------
void LoopbackProxy::stop()
// Stop the threads of the proxy and join them
{
    _stop = true;
    if (_thread.joinable())
        _thread.join();
    for (auto& relay : _relays)
        relay.join();
}
//---------------------------------------------------------------------------
bool LoopbackProxy::write(int fd, const char* data, uint64_t size) const
// Write the whole buffer to the socket, false once the peer is gone
{
    for (uint64_t written = 0; written != size;) {
        // MSG_NOSIGNAL keeps a closed peer from killing the process
        auto sent = ::send(fd, data + written, size - written, MSG_NOSIGNAL);
        if (sent > 0)
            written += static_cast<uint64_t>(sent);
        else if (_stop || !retryable())
            return false;
    }
    return true;
}
//---------------------------------------------------------------------------
void LoopbackProxy::park() const
// Wait until the proxy stops
{
    while (!_stop)
        this_thread::sleep_for(blockingTimeout);
}
//---------------------------------------------------------------------------
void LoopbackProxy::bound(int fd)
// Bound the blocking calls of a socket
{
    timeval timeout = {.tv_sec = 0, .tv_usec = chrono::microseconds(blockingTimeout).count()};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}
//---------------------------------------------------------------------------
void LoopbackProxy::resetOnClose(int fd)
// Turn the close of a socket into a tcp reset
{
    linger immediate = {.l_onoff = 1, .l_linger = 0};
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &immediate, sizeof(immediate));
}
//---------------------------------------------------------------------------
bool LoopbackProxy::retryable()
// Check whether the failed syscall should be retried
{
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}
//---------------------------------------------------------------------------
} // namespace anyblob::test
