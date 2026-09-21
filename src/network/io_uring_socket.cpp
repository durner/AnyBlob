#ifndef ANYBLOB_HAS_IO_URING
#error "Cannot build io_uring_socket.cpp when compiling without uring support"
#endif
#include "network/io_uring_socket.hpp"
#include <cassert>
#include <cerrno>
#include <cstring>
#include <stdexcept>
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
using namespace std;
//---------------------------------------------------------------------------
IOUringSocket::IOUringSocket(uint32_t entries, int32_t /*flags*/)
// Constructor that inits uring queue
{
    // initialize io_uring
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));

    if (io_uring_queue_init_params(entries, &_uring, &params) < 0) {
        throw runtime_error("Uring init error!");
    }

    if (!(params.features & IORING_FEAT_FAST_POLL)) {
        throw runtime_error("Uring init error - IORING_FEAT_FAST_POLL not available in the kernel!");
    }
}
//---------------------------------------------------------------------------
void IOUringSocket::prepare(Request& req, chrono::milliseconds timeout, int32_t msg_flags)
// Prepare a submission (sqe)
{
    assert(req.length > 0);
    auto sqe = io_uring_get_sqe(&_uring);
    if (req.event == EventType::write)
        io_uring_prep_send(sqe, req.fd, req.data.cdata, static_cast<uint64_t>(req.length), msg_flags | MSG_NOSIGNAL);
    else
        io_uring_prep_recv(sqe, req.fd, req.data.data, static_cast<uint64_t>(req.length), msg_flags);
    io_uring_sqe_set_data(sqe, &req);
    if (!timeout.count())
        return;
    sqe->flags |= IOSQE_IO_LINK;
    req.kernelTimeout = toKernelTimespec(timeout);
    auto timeoutSqe = io_uring_get_sqe(&_uring);
    io_uring_prep_link_timeout(timeoutSqe, &req.kernelTimeout, 0);
    io_uring_sqe_set_data(timeoutSqe, nullptr);
}
//---------------------------------------------------------------------------
void IOUringSocket::prepareTimer(Request& req, chrono::milliseconds delay)
// Prepare a timer submission
{
    req.event = EventType::timer;
    req.kernelTimeout = toKernelTimespec(delay);
    auto sqe = io_uring_get_sqe(&_uring);
    io_uring_prep_timeout(sqe, &req.kernelTimeout, 0, 0);
    io_uring_sqe_set_data(sqe, &req);
}
//---------------------------------------------------------------------------
void IOUringSocket::processImpl()
// Submit the queued requests and append completed CQE
{
    auto submitted = io_uring_submit_and_wait(&_uring, hasOutstanding() ? 1u : 0u);
    if (submitted < 0 && submitted != -EINTR && submitted != -EAGAIN)
        throw runtime_error("socket submit error: " + to_string(-submitted));
    if (submitted > 0)
        _outstanding += static_cast<uint32_t>(submitted);

    // Drain the whole completion queue
    uint32_t collected = 0;
    uint32_t head;
    io_uring_cqe* cqe;
    io_uring_for_each_cqe(&_uring, head, cqe) {
        collected++;
        // A timeout carries no request
        if (auto req = static_cast<Request*>(io_uring_cqe_get_data(cqe))) {
            req->length = cqe->res;
            _completions.push_back(req->messageTask);
        }
    }
    io_uring_cq_advance(&_uring, collected);
    _outstanding -= collected;
}
//---------------------------------------------------------------------------
IOUringSocket::~IOUringSocket() noexcept
// The destructor
{
    io_uring_queue_exit(&_uring);
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
