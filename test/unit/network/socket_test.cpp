#include "catch2/single_include/catch2/catch.hpp"
#include "network/connection_manager.hpp"
#ifdef ANYBLOB_HAS_IO_URING
#include "network/io_uring_socket.hpp"
#endif
#include "perfevent/PerfEvent.hpp"
#include <vector>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network::test {
//---------------------------------------------------------------------------
TEST_CASE("io_uring_socket") {
    PerfEventBlock e;
    ConnectionManager io(10);
    ConnectionManager::TCPSettings tcpSettings;
    REQUIRE(io.connect("db.in.tum.de", 80, false, false, tcpSettings) > 0);
}
//---------------------------------------------------------------------------
#ifdef ANYBLOB_HAS_IO_URING
TEST_CASE("kernel_timespec_normalization") {
    auto ts = IOUringSocket::toKernelTimespec(std::chrono::milliseconds(1500));
    REQUIRE(ts.tv_sec == 1);
    REQUIRE(ts.tv_nsec == 500'000'000);
    ts = IOUringSocket::toKernelTimespec(std::chrono::milliseconds(500));
    REQUIRE(ts.tv_sec == 0);
    REQUIRE(ts.tv_nsec == 500'000'000);
}
//---------------------------------------------------------------------------
TEST_CASE("io_uring_zero_copy_send") {
    // Zero copy needs tcp, so connect over loopback
    auto listener = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{.sin_family = AF_INET, .sin_port = 0, .sin_addr = {htonl(INADDR_LOOPBACK)}, .sin_zero = {}};
    socklen_t addrLen = sizeof(addr);
    REQUIRE(!bind(listener, reinterpret_cast<sockaddr*>(&addr), addrLen));
    REQUIRE(!listen(listener, 1));
    REQUIRE(!getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &addrLen));
    auto sender = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(!connect(sender, reinterpret_cast<sockaddr*>(&addr), addrLen));
    auto receiver = accept(listener, nullptr, nullptr);

    std::vector<uint8_t> data(1 << 16, 42);
    auto length = static_cast<int64_t>(data.size());
    IOUringSocket uring(8);
    Socket::Request req{.data = {.cdata = data.data()}, .length = length, .fd = sender, .event = Socket::EventType::write, .messageTask = nullptr, .zeroCopy = true};
    uring.prepare(req, std::chrono::milliseconds(0));
    // The task finishes once, after the notification
    uint32_t completed = 0;
    while (uring.hasOutstanding())
        uring.process([&](MessageTask*) { completed++; });
    REQUIRE(completed == 1);
    REQUIRE(req.length == length);
    REQUIRE(recv(receiver, data.data(), data.size(), MSG_WAITALL) == length);
    close(sender);

    close(receiver);
    close(listener);
}
#endif
//---------------------------------------------------------------------------
} // namespace anyblob::network::test
