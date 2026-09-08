#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <netinet/in.h>
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
/// This class relays tcp connections to a target on its own threads and injects a network fault into them
class FaultProxy {
    public:
    /// The injected fault
    enum class Mode : uint8_t {
        /// Reset right after the accept
        rstOnConnect,
        /// Accept and never answer
        acceptHang,
        /// Answer with bytes that are not http
        garbage,
        /// Forward arg bytes, then stop answering
        headerStall,
        /// Forward arg bytes, then close
        closeMidBody,
        /// Forward arg bytes, then reset
        rstMidBody,
        /// Delay the answer by arg milliseconds
        responseDelay,
        /// Pace the answer at arg bytes per second
        trickle,
        /// Reset the target after arg uploaded bytes
        rstMidUpload,
        /// Close after arg milliseconds without traffic
        idleClose
    };

    private:
    /// The write size of the paced answer
    static constexpr uint64_t pieceSize = 1024;
    /// The bound of every blocking call
    static constexpr std::chrono::milliseconds blockingTimeout{100};

    /// The target address
    sockaddr_in _target = {};
    /// The listening socket
    int _listenFd = -1;
    /// The listen port
    uint16_t _port = 0;
    /// The fault
    Mode _mode;
    /// The fault parameter in bytes, milliseconds, or bytes per second
    uint64_t _arg;
    /// Fault only the first n connections, negative faults all
    int _faults;
    /// The accepted connections
    std::atomic<uint64_t> _accepted = 0;
    /// The stop signal of all threads
    std::atomic<bool> _stop = false;
    /// The accept thread
    std::thread _thread;
    /// The relay threads
    std::vector<std::thread> _relays;

    public:
    /// The constructor listens on a free loopback port and starts the proxy
    FaultProxy(const std::string& target, Mode mode, uint64_t arg = 0, int faults = -1);
    /// The destructor stops the proxy and closes all sockets
    ~FaultProxy();

    /// Get the endpoint of the proxy
    [[nodiscard]] std::string getEndpoint() const { return "127.0.0.1:" + std::to_string(_port); }
    /// Get the number of accepted connections
    [[nodiscard]] uint64_t getAccepted() const { return _accepted.load(std::memory_order_relaxed); }
    /// Get the name of a fault for the test output
    [[nodiscard]] static std::string_view getName(Mode mode);

    private:
    /// Accept the connections until the proxy stops
    void accept();
    /// Relay one connection and inject the faults of the handshake
    void relay(int client, bool faulty);
    /// Forward one direction and inject the faults of the transfer
    void pump(int from, int to, bool faulty, bool downstream, std::atomic<uint64_t>& activity);
    /// Write the whole buffer, false once the peer is gone
    bool write(int fd, const char* data, uint64_t size);
    /// Wait until the proxy stops
    void park() const;
    /// Bound the blocking calls of a socket
    static void bound(int fd);
    /// Turn the close of a socket into a tcp reset
    static void resetOnClose(int fd);
    /// Check whether the failed syscall should be retried
    static bool retryable();
};
//---------------------------------------------------------------------------
} // namespace anyblob::test
