#pragma once
#include <chrono>
#include <cstdint>
#include <vector>
#ifdef ANYBLOB_HAS_IO_URING
#include <liburing.h>
#endif
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// CedarDB (Dominik Durner), 2025
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network {
//---------------------------------------------------------------------------
struct MessageTask;
//---------------------------------------------------------------------------
/// This is the interface for classes that open a TCP connection to a server
/// and exchanges messages.
//---------------------------------------------------------------------------
class Socket {
    public:
    /// Request message event type
    enum class EventType : uint8_t {
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
#ifdef ANYBLOB_HAS_IO_URING
        /// The kernel async interface timeout
        __kernel_timespec kernelTimeout = {.tv_sec = 0, .tv_nsec = 0};
#endif
    };

    /// The destructor
    virtual ~Socket() noexcept = default;
    /// Prepare a submission (a zero timeout waits indefinitely)
    virtual void prepare(Request& req, std::chrono::milliseconds timeout, int32_t msg_flags = 0) = 0;
    /// Submit the queued requests and run the callback on the completed transfers.
    /// Blocks for the first completion unless nothing is outstanding.
    template <typename F>
    void process(F&& onCompletion) {
        _completions.clear();
        processImpl();
        for (auto task : _completions)
            onCompletion(task);
    }
    /// Whether a transfer is queued or still in flight
    [[nodiscard]] virtual bool hasOutstanding() const = 0;

    protected:
    /// Submit the queued requests and append finished tasks
    virtual void processImpl() = 0;

    /// The completed message tasks
    std::vector<MessageTask*> _completions;
};
//---------------------------------------------------------------------------
} // namespace anyblob::network
