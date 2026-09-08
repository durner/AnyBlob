#include "fault_proxy.hpp"
#include <algorithm>
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
FaultProxy::FaultProxy(const string& target, Mode mode, uint64_t arg, int faults) : _mode(mode), _arg(arg), _faults(faults)
// Resolve the target, listen on a free loopback port, and start the proxy
{
    auto colon = target.rfind(':');
    if (colon == string::npos)
        throw runtime_error("fault proxy needs a host:port target");
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* resolved = nullptr;
    if (getaddrinfo(target.substr(0, colon).c_str(), target.substr(colon + 1).c_str(), &hints, &resolved))
        throw runtime_error("fault proxy cannot resolve " + target);
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
        throw runtime_error("fault proxy cannot listen on loopback");
    }
    bound(_listenFd);
    _port = ntohs(address.sin_port);
    _thread = thread([this]() { accept(); });
}
//---------------------------------------------------------------------------
FaultProxy::~FaultProxy()
// Stop the proxy and close all sockets
{
    _stop = true;
    _thread.join();
    for (auto& relay : _relays)
        relay.join();
    ::close(_listenFd);
}
//---------------------------------------------------------------------------
string_view FaultProxy::getName(Mode mode)
// Get the name of a fault for the test output
{
    switch (mode) {
        case Mode::rstOnConnect: return "rst-on-connect";
        case Mode::acceptHang: return "accept-hang";
        case Mode::garbage: return "garbage";
        case Mode::headerStall: return "header-stall";
        case Mode::closeMidBody: return "close-mid-body";
        case Mode::rstMidBody: return "rst-mid-body";
        case Mode::responseDelay: return "response-delay";
        case Mode::trickle: return "trickle";
        case Mode::rstMidUpload: return "rst-mid-upload";
        case Mode::idleClose: return "idle-close";
    }
    return "unknown";
}
//---------------------------------------------------------------------------
void FaultProxy::accept()
// Accept the connections until the proxy stops
{
    while (!_stop) {
        auto client = ::accept(_listenFd, nullptr, nullptr);
        if (client < 0)
            continue;
        bound(client);
        auto accepted = _accepted.fetch_add(1, memory_order_relaxed);
        auto faulty = _faults < 0 || accepted < static_cast<uint64_t>(_faults);
        _relays.push_back(thread([this, client, faulty]() { relay(client, faulty); }));
    }
}
//---------------------------------------------------------------------------
void FaultProxy::relay(int client, bool faulty)
// Relay one connection and inject the faults of the handshake
{
    auto server = -1;
    // The handshake faults never reach the target
    if (faulty && _mode == Mode::rstOnConnect) {
        resetOnClose(client);
    } else if (faulty && _mode == Mode::acceptHang) {
        park();
    } else if (faulty && _mode == Mode::garbage) {
        string garbage;
        for (auto i = 0u; i != 16u; i++)
            garbage += "XYZZY this is not http\r\n";
        vector<char> request(64u << 10);
        if (::recv(client, request.data(), request.size(), 0) > 0)
            write(client, garbage.data(), garbage.size());
    } else {
        server = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (server >= 0 && !::connect(server, reinterpret_cast<sockaddr*>(&_target), sizeof(_target))) {
            bound(server);
            atomic<uint64_t> activity = 0;
            // The read shutdown ends the pump of the other direction
            auto upstream = thread([&]() { pump(client, server, faulty, false, activity); ::shutdown(server, SHUT_RD); });
            pump(server, client, faulty, true, activity);
            ::shutdown(client, SHUT_RD);
            upstream.join();
        }
    }
    if (server >= 0)
        ::close(server);
    ::close(client);
}
//---------------------------------------------------------------------------
void FaultProxy::pump(int from, int to, bool faulty, bool downstream, atomic<uint64_t>& activity)
// Forward one direction and inject the faults of the transfer
{
    vector<char> buffer(64u << 10);
    auto paceStart = chrono::steady_clock::now();
    auto idleStart = paceStart;
    uint64_t idleMark = 0;
    uint64_t moved = 0;
    while (!_stop) {
        auto received = ::recv(from, buffer.data(), buffer.size(), 0);
        if (!received || (received < 0 && !retryable()))
            return;
        if (received < 0) {
            if (faulty && _mode == Mode::idleClose) {
                if (activity != idleMark) {
                    idleMark = activity;
                    idleStart = chrono::steady_clock::now();
                } else if (chrono::steady_clock::now() - idleStart > chrono::milliseconds(static_cast<int64_t>(_arg)))
                    return;
            }
            continue;
        }
        auto length = static_cast<uint64_t>(received);
        if (faulty && downstream) {
            // The cut faults answer up to arg bytes
            if ((_mode == Mode::headerStall || _mode == Mode::closeMidBody || _mode == Mode::rstMidBody) && moved + length > _arg) {
                write(to, buffer.data(), _arg > moved ? _arg - moved : 0);
                if (_mode == Mode::rstMidBody)
                    resetOnClose(to);
                if (_mode == Mode::headerStall)
                    park();
                return;
            }
            if (_mode == Mode::responseDelay && !moved)
                this_thread::sleep_for(chrono::milliseconds(static_cast<int64_t>(_arg)));
            if (_mode == Mode::trickle) {
                if (!moved)
                    paceStart = chrono::steady_clock::now();
                for (uint64_t piece = 0; piece != length;) {
                    auto bytes = min(pieceSize, length - piece);
                    if (!write(to, buffer.data() + piece, bytes))
                        return;
                    piece += bytes;
                    moved += bytes;
                    activity += bytes;
                    this_thread::sleep_until(paceStart + chrono::microseconds(static_cast<int64_t>(moved * 1000000 / max<uint64_t>(1, _arg))));
                }
                continue;
            }
        }
        // The upload reset hits the target socket
        if (faulty && !downstream && _mode == Mode::rstMidUpload && moved + length > _arg) {
            resetOnClose(to);
            return;
        }
        if (!write(to, buffer.data(), length))
            return;
        moved += length;
        activity += length;
    }
}
//---------------------------------------------------------------------------
bool FaultProxy::write(int fd, const char* data, uint64_t size)
// Write the whole buffer, false once the peer is gone
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
void FaultProxy::park() const
// Wait until the proxy stops
{
    while (!_stop)
        this_thread::sleep_for(blockingTimeout);
}
//---------------------------------------------------------------------------
void FaultProxy::bound(int fd)
// Bound the blocking calls of a socket
{
    timeval timeout = {.tv_sec = 0, .tv_usec = chrono::microseconds(blockingTimeout).count()};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}
//---------------------------------------------------------------------------
void FaultProxy::resetOnClose(int fd)
// Turn the close of a socket into a tcp reset
{
    linger immediate = {.l_onoff = 1, .l_linger = 0};
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &immediate, sizeof(immediate));
}
//---------------------------------------------------------------------------
bool FaultProxy::retryable()
// Check whether the failed syscall should be retried
{
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}
//---------------------------------------------------------------------------
} // namespace anyblob::test
