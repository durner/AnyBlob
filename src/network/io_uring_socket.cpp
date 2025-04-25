#ifdef ANYBLOB_HAS_IO_URING
#include "network/io_uring_socket.hpp"
#include <cassert>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/eventfd.h>
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

    _eventId = eventfd(0, 0);
    if (_eventId < 0)
        throw runtime_error("Event FD creation error!");
    io_uring_register_eventfd(&_uring, _eventId);
}
//---------------------------------------------------------------------------
io_uring_sqe* IOUringSocket::send_prep(const Request* req, int32_t msg_flags, uint8_t flags)
// Prepare a submission (sqe) send
{
    assert(req->length > 0);
    auto sqe = io_uring_get_sqe(&_uring);
    io_uring_prep_send(sqe, req->fd, req->data.cdata, static_cast<uint64_t>(req->length), msg_flags);
    sqe->flags |= flags;
    sqe->user_data = reinterpret_cast<uintptr_t>(req);
    return sqe;
}
//---------------------------------------------------------------------------
io_uring_sqe* IOUringSocket::recv_prep(Request* req, int32_t msg_flags, uint8_t flags)
// Prepare a submission (sqe) recv
{
    assert(req->length > 0);
    auto sqe = io_uring_get_sqe(&_uring);
    io_uring_prep_recv(sqe, req->fd, req->data.data, static_cast<uint64_t>(req->length), msg_flags);
    sqe->flags |= flags;
    sqe->user_data = reinterpret_cast<uintptr_t>(req);
    return sqe;
}
//---------------------------------------------------------------------------
io_uring_sqe* IOUringSocket::send_prep_to(const Request* req, __kernel_timespec* timeout, int32_t msg_flags, uint8_t flags)
// Prepare a submission (sqe) send with relative timeout
{
    assert(req->length > 0);
    auto sqe = io_uring_get_sqe(&_uring);
    io_uring_prep_send(sqe, req->fd, req->data.cdata, static_cast<uint64_t>(req->length), msg_flags);
    sqe->flags |= flags | IOSQE_IO_LINK;
    sqe->user_data = reinterpret_cast<uintptr_t>(req);
    auto timeoutSqe = io_uring_get_sqe(&_uring);
    io_uring_prep_link_timeout(timeoutSqe, timeout, 0);
    timeoutSqe->user_data = reinterpret_cast<uintptr_t>(nullptr);
    return sqe;
}
//---------------------------------------------------------------------------
io_uring_sqe* IOUringSocket::recv_prep_to(Request* req, __kernel_timespec* timeout, int32_t msg_flags, uint8_t flags)
// Prepare a submission (sqe) recv with relative timeout
{
    assert(req->length > 0);
    auto sqe = io_uring_get_sqe(&_uring);
    io_uring_prep_recv(sqe, req->fd, req->data.data, static_cast<uint64_t>(req->length), msg_flags);
    sqe->flags |= flags | IOSQE_IO_LINK;
    sqe->user_data = reinterpret_cast<uintptr_t>(req);
    auto timeoutSqe = io_uring_get_sqe(&_uring);
    io_uring_prep_link_timeout(timeoutSqe, timeout, 0);
    timeoutSqe->user_data = reinterpret_cast<uintptr_t>(nullptr);
    return sqe;
}
//---------------------------------------------------------------------------
IOUringSocket::Request* IOUringSocket::peek()
// Get a completion (cqe) event if it is available and mark it as seen; return the SQE attached Request if available else nullptr
{
    io_uring_cqe* cqe;
    auto res = io_uring_peek_cqe(&_uring, &cqe);
    if (!res) {
        Request* req = nullptr;
        memcpy(&req, &cqe->user_data, sizeof(cqe->user_data));
        if (req)
            req->length = cqe->res;
        seen(cqe);
        return req;
    }
    return nullptr;
}
//---------------------------------------------------------------------------
IOUringSocket::Request* IOUringSocket::complete()
// Get a completion (cqe) event and mark it as seen; return the SQE attached Request
{
    io_uring_cqe* cqe;
    auto res = io_uring_wait_cqe(&_uring, &cqe);
    if (!res) {
        Request* req = nullptr;
        memcpy(&req, &cqe->user_data, sizeof(cqe->user_data));
        if (req)
            req->length = cqe->res;
        seen(cqe);
        return req;
    }
    throw runtime_error("io_uring_wait_cqe error!");
}
//---------------------------------------------------------------------------
io_uring_cqe* IOUringSocket::completion()
// Get a completion (cqe) event
{
    io_uring_cqe* cqe;
    auto res = io_uring_wait_cqe(&_uring, &cqe);
    if (!res) {
        return cqe;
    }
    throw runtime_error("Completion error!");
}
//---------------------------------------------------------------------------
uint32_t IOUringSocket::submitCompleteAll(uint32_t events, vector<IOUringSocket::Request*>& completions)
// Submits queue and gets all completion (cqe) event and mark them as seen; return the SQE attached requests
{
    uint32_t count = 0;
    while (events > count) {
        io_uring_submit_and_wait(&_uring, 1);
        io_uring_cqe* cqe;
        uint32_t head;
        uint32_t localCount = 0;

        // iterate all cqes
        io_uring_for_each_cqe(&_uring, head, cqe) {
            localCount++;
            Request* req = nullptr;
            memcpy(&req, &cqe->user_data, sizeof(cqe->user_data));
            if (req)
                req->length = cqe->res;
            completions.push_back(req);
        }
        io_uring_cq_advance(&_uring, localCount);
        count += localCount;
    }
    return count;
}
//---------------------------------------------------------------------------
void IOUringSocket::seen(io_uring_cqe* cqe)
// Mark a completion (cqe) event seen to allow for new completions in the kernel
{
    io_uring_cqe_seen(&_uring, cqe);
}
//---------------------------------------------------------------------------
int32_t IOUringSocket::submit()
// Submit uring to the kernel and return the number of submitted entries
{
    return io_uring_submit(&_uring);
}
//---------------------------------------------------------------------------
void IOUringSocket::wait()
// Waits for an cqe event in the uring
{
    eventfd_t v;
    int ret = eventfd_read(_eventId, &v);
    if (ret < 0)
        throw runtime_error("Wait error!");
}
//---------------------------------------------------------------------------
IOUringSocket::~IOUringSocket() noexcept
// The destructor
{
    io_uring_queue_exit(&_uring);
    close(_eventId);
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
#endif
