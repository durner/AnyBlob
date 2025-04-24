#pragma once
#include "network/socket.hpp"
#include <chrono>
#include <unordered_map>
#include <vector>
#include <sys/poll.h>
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
/// This class opens a TCP connection to a server and exchanges messages
/// with the old poll interface acting as a fallback.
/// We implement it to fully match the IOUringSockets behavior.
class PollSocket : public Socket {
    private:
    /// The request infos with timeout and message flags
    struct RequestInfo {
        /// The real request
        Request* request;
        /// The timeout in ms
        std::chrono::time_point<std::chrono::steady_clock> timeout;
        /// The flags
        int32_t flags;
    };
    /// The ready request vector
    std::vector<Request*> ready;
    /// The fd to request mapping
    std::unordered_map<int, RequestInfo> fdToRequest;
    /// The pollfd vector
    std::vector<pollfd> pollfds;
    /// The submitted requests since last invocation
    int32_t submitted = 0;

    public:
    /// The destructor
    ~PollSocket() noexcept override = default;

    /// Prepare a submission send
    bool send(const Request* req, int32_t msg_flags = 0) override;
    /// Prepare a submission recv
    bool recv(Request* req, int32_t msg_flags = 0) override;
    /// Prepare a submission send with timeout
    bool send_to(const Request* req, __kernel_timespec* timeout, int32_t msg_flags = 0) override;
    /// Prepare a submission recv with timeout
    bool recv_to(Request* req, __kernel_timespec* timeout, int32_t msg_flags = 0) override;

    /// Get a completion event and mark it as seen; return the Request
    Request* complete() override;
    /// Submit the request to the kernel
    int32_t submit() override;

    private:
    /// Implement the fd into our submission queue
    void enqueue(int fd, short events, RequestInfo req);
};
//---------------------------------------------------------------------------
} // namespace anyblob::network
