#include "fault_proxy.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>
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
FaultProxy::FaultProxy(const string& target, Mode mode, uint64_t arg, int faults) : LoopbackProxy(target, "fault"), _mode(mode), _arg(arg), _faults(faults)
// The constructor listens on a free loopback port and starts the proxy
{
    start([this](int client, uint64_t index) { relay(client, index); });
}
//---------------------------------------------------------------------------
FaultProxy::~FaultProxy()
// The destructor stops the proxy and closes all sockets
{
    stop();
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
        case Mode::uploadStall: return "upload-stall";
        case Mode::corruptStream: return "corrupt-stream";
        case Mode::idleClose: return "idle-close";
        case Mode::gatewayError: return "gateway-error";
    }
    return "unknown";
}
//---------------------------------------------------------------------------
void FaultProxy::relay(int client, uint64_t index)
// Relay one connection and inject the faults of the handshake
{
    bound(client);
    auto faulty = _faults < 0 || index < static_cast<uint64_t>(_faults);
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
    } else if (faulty && _mode == Mode::gatewayError) {
        string response = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        vector<char> request(64u << 10);
        if (::recv(client, request.data(), request.size(), 0) > 0)
            write(client, response.data(), response.size());
    } else {
        if (faulty && _mode == Mode::uploadStall) {
            int receiveBuffer = 8 << 10;
            setsockopt(client, SOL_SOCKET, SO_RCVBUF, &receiveBuffer, sizeof(receiveBuffer));
        }
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
            if (_mode == Mode::corruptStream && moved + length > _arg && moved <= _arg) {
                buffer[_arg - moved] = static_cast<char>(buffer[_arg - moved] ^ 0xff);
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
        if (faulty && !downstream && _mode == Mode::uploadStall && moved + length > _arg) {
            park();
            return;
        }
        if (!write(to, buffer.data(), length))
            return;
        moved += length;
        activity += length;
    }
}
//---------------------------------------------------------------------------
} // namespace anyblob::test
