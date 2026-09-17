#pragma once
#ifndef ANYBLOB_HAS_IO_URING
#error "You must not include io_uring_socket.hpp when building without uring support"
#endif
#include "network/socket.hpp"
#include <chrono>
#include <cstdint>
#include <liburing.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network {
//---------------------------------------------------------------------------
struct MessageTask;
//---------------------------------------------------------------------------
/// This exchanges messages with the new io_uring library for minimizing
/// syscalls and unnecessary kernel overhead
class IOUringSocket : public Socket {
    private:
    /// The uring buffer
    struct io_uring _uring;
    /// The outstanding entries
    uint32_t _outstanding = 0;

    /// Submit the queued requests and append finished tasks
    void processImpl() override;

    public:
    /// The IO Uring Socket Constructor
    explicit IOUringSocket(uint32_t entries, int32_t flags = 0);
    /// The destructor
    ~IOUringSocket() noexcept override;

    /// Prepare a submission (sqe) event
    void prepare(Request& req, std::chrono::milliseconds timeout, int32_t msg_flags = 0) override;
    /// Convert a timeout into kernel timespec
    static constexpr __kernel_timespec toKernelTimespec(std::chrono::milliseconds timeout) {
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count();
        return {ns / 1'000'000'000, ns % 1'000'000'000};
    }

    /// Whether a transfer is queued or still in flight
    [[nodiscard]] bool hasOutstanding() const override { return _outstanding || io_uring_sq_ready(&_uring); }
};
//---------------------------------------------------------------------------
} // namespace anyblob::network
