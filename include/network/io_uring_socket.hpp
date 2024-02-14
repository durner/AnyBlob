#pragma once
#include <memory>
#include <string>
#include <vector>
#include <liburing.h>
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
    /// The event id for the uring
    int _eventId;

    public:
    /// The IO Uring Socket Constructor
    explicit IOUringSocket(uint32_t entries, int32_t flags = 0);
    /// The destructor
    ~IOUringSocket();

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

    /// Submit uring to the kernel and return the number of submitted entries
    int32_t submit();
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
