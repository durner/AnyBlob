#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
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
/// This class accepts loopback connections on a free port and relays each of them on its own thread
class LoopbackProxy {
    protected:
    /// The bound of every blocking call
    static constexpr std::chrono::milliseconds blockingTimeout{100};

    /// The target address
    sockaddr_in _target = {};
    /// The stop signal of all threads
    std::atomic<bool> _stop = false;

    /// Resolve the target and listen on a free loopback port
    LoopbackProxy(const std::string& target, const char* name);
    /// Close the listening socket, the derived destructor stops the threads
    ~LoopbackProxy();

    /// Hand every accepted connection and its index to the relay until the proxy stops
    void start(std::function<void(int, uint64_t)> relay);
    /// Stop the threads of the proxy and join them
    void stop();
    /// Write the whole buffer to the socket, false once the peer is gone
    bool write(int fd, const char* data, uint64_t size) const;
    /// Wait until the proxy stops
    void park() const;
    /// Bound the blocking calls of a socket
    static void bound(int fd);
    /// Turn the close of a socket into a tcp reset
    static void resetOnClose(int fd);
    /// Check whether the failed syscall should be retried
    static bool retryable();

    public:
    /// Get the endpoint of the proxy
    [[nodiscard]] std::string getEndpoint() const { return "127.0.0.1:" + std::to_string(_port); }
    /// Get the number of accepted connections
    [[nodiscard]] uint64_t getAccepted() const { return _accepted.load(std::memory_order_relaxed); }

    private:
    /// The listening socket
    int _listenFd = -1;
    /// The listen port
    uint16_t _port = 0;
    /// The number of accepted connections
    std::atomic<uint64_t> _accepted = 0;
    /// The accept thread
    std::thread _thread;
    /// The relay threads
    std::vector<std::thread> _relays;
};
//---------------------------------------------------------------------------
} // namespace anyblob::test
