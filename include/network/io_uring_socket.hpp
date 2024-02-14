#pragma once
#include "network/resolver.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <liburing.h>
#include <netdb.h>
#include <netinet/tcp.h>
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
struct MessageTask;
//---------------------------------------------------------------------------
/// This class opens a TCP connection to a server and exchanges messages
/// with the new io_uring library for minimizing syscalls and unnecessary
/// kernel overhead
class IOUringSocket {
    public:
    /// Request message event type
    enum class EventType : uint32_t {
        read = 0,
        write = 1
    };

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
        int reuse = 0;
        /// The kernel timeout parameter
        __kernel_timespec kernelTimeout;

        TCPSettings() {
            kernelTimeout.tv_sec = 0;
            kernelTimeout.tv_nsec = timeout * 1000;
        }
    };

    /// The request message
    struct Request {
        union Data {
            /// The recv data
            uint8_t* data;
            /// The send data
            const uint8_t* cdata;
        };
        /// The data
        Data data;
        /// The length
        int64_t length;
        /// The file descriptor
        int32_t fd;
        /// Specifies the event
        EventType event;
        /// The associated message task
        MessageTask* messageTask;
    };

    private:
    /// The uring buffer
    struct io_uring _uring;
    /// The tcp socket
    std::unordered_set<int32_t> _fdSockets;
    /// The event id for the uring
    int _eventId;
    /// The fd cache
    std::unordered_multimap<std::string, int32_t> _fdCache;
    /// Resolver
    std::unordered_map<std::string, std::unique_ptr<Resolver>> _resolverCache;

    public:
    /// The IO Uring Socket Constructor
    IOUringSocket(uint32_t entries, int32_t flags = 0);
    /// The destructor
    ~IOUringSocket();

    /// Creates a new socket connection
    [[nodiscard]] int32_t connect(std::string hostname, uint32_t port, TCPSettings& tcpSettings, int retryLimit = 16);
    /// Disconnects the socket
    void disconnect(int32_t fd, std::string hostname = "", uint32_t port = 0, TCPSettings* tcpSettings = nullptr, uint64_t bytes = 0, bool forceShutdown = false);

    /// Add resolver
    void addResolver(const std::string& hostname, std::unique_ptr<Resolver> resolver);

    /// Prepare a submission (sqe) send
    io_uring_sqe* send_prep(const Request* req, int32_t msg_flags = 0, uint8_t flags = 0);
    /// Prepare a submission (sqe) recv
    io_uring_sqe* recv_prep(Request* req, int32_t msg_flags = 0, uint8_t flags = 0);
    /// Prepare a submission (sqe) send with timeout
    io_uring_sqe* send_prep_to(const Request* req, __kernel_timespec* timeout, int32_t msg_flags = 0, uint8_t flags = 0);
    /// Prepare a submission (sqe) recv with timeout
    io_uring_sqe* recv_prep_to(Request* req, __kernel_timespec* timeout, int32_t msg_flags = 0, uint8_t flags = 0);

    /// Submits queue and gets all completion (cqe) event and mark them as seen; return the SQE attached requests
    uint32_t submitCompleteAll(uint32_t events, std::vector<IOUringSocket::Request*>& completions);
    /// Get a completion (cqe) event and mark it as seen; return the SQE attached Request
    [[nodiscard]] Request* complete();
    /// Get a completion (cqe) event if it is available and mark it as seen; return the SQE attached Request if available else nullptr
    Request* peek();
    /// Get a completion (cqe) event
    [[nodiscard]] io_uring_cqe* completion();
    /// Mark a completion (cqe) event seen to allow for new completions in the kernel
    void seen(io_uring_cqe* cqe);
    /// Wait for a new cqe event arriving
    void wait();
    /// Checks for a timeout
    bool checkTimeout(int fd, TCPSettings& settings);

    /// Submit uring to the kernel and return the number of submitted entries
    int32_t submit();
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
