#include "network/poll_socket.hpp"
#include <cerrno>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
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
using namespace std;
//---------------------------------------------------------------------------
bool PollSocket::send(const Request* req, int32_t msg_flags)
// Prepare a submission send
{
    if (!req || req->event != EventType::write) return false;
    enqueue(req->fd, POLLOUT, RequestInfo{.request = const_cast<Request*>(req), .timeout = chrono::time_point<chrono::steady_clock>::max(), .flags = msg_flags});
    return true;
}
//---------------------------------------------------------------------------
bool PollSocket::recv(Request* req, int32_t msg_flags)
// Prepare a submission recv
{
    if (!req || req->event != EventType::read) return false;
    enqueue(req->fd, POLLIN, RequestInfo{.request = req, .timeout = chrono::time_point<chrono::steady_clock>::max(), .flags = msg_flags});
    return true;
}
//---------------------------------------------------------------------------
bool PollSocket::send_to(const Request* req, __kernel_timespec* timeout, int32_t msg_flags)
// Prepare a submission send with timeout
{
    if (!req || req->event != EventType::write) return false;
    auto duration = chrono::seconds(timeout->tv_sec) + chrono::nanoseconds(timeout->tv_nsec);
    enqueue(req->fd, POLLOUT, RequestInfo{.request = const_cast<Request*>(req), .timeout = chrono::steady_clock::now() + duration, .flags = msg_flags});
    return true;
}
//---------------------------------------------------------------------------
bool PollSocket::recv_to(Request* req, __kernel_timespec* timeout, int32_t msg_flags)
// Prepare a submission recv with timeout
{
    if (!req || req->event != EventType::read) return false;
    auto duration = chrono::seconds(timeout->tv_sec) + chrono::nanoseconds(timeout->tv_nsec);
    enqueue(req->fd, POLLIN, RequestInfo{.request = req, .timeout = chrono::steady_clock::now() + duration, .flags = msg_flags});
    return true;
}
//---------------------------------------------------------------------------
PollSocket::Request* PollSocket::complete()
// Get a completion event and mark it as seen; return the Request
{
    while (ready.empty()) {
        // Poll wait up to 1ms
        int ret = ::poll(pollfds.data(), pollfds.size(), 1);
        // No events ready
        if (ret <= 0) continue;

        // Check for completed events
        auto currentTime = chrono::steady_clock::now();
        for (auto pit = pollfds.begin(); pit != pollfds.end();) {
            if (auto it = fdToRequest.find(pit->fd); it != fdToRequest.end()) {
                auto& req = it->second;
                if (pit->revents & (POLLIN | POLLOUT)) {
                    // Active pollfd
                    if (req.request->event == EventType::read) {
                        req.request->length = ::recv(it->first, req.request->data.data, static_cast<size_t>(req.request->length), req.flags);
                    } else if (req.request->event == EventType::write) {
                        req.request->length = ::send(it->first, req.request->data.cdata, static_cast<size_t>(req.request->length), req.flags);
                    }

                    // Simulate io uring by returning -errno
                    if (req.request->length == -1)
                        req.request->length = -errno;

                    ready.push_back(req.request);
                    fdToRequest.erase(it->first);
                    pit = pollfds.erase(pit);
                } else if (req.timeout < currentTime) {
                    // Implement timeout
                    req.request->length = -ETIMEDOUT;
                    ready.push_back(req.request);
                    fdToRequest.erase(it->first);
                    pit = pollfds.erase(pit);
                } else {
                    ++pit;
                }
            } else {
                throw runtime_error("couldn't find request");
            }
        }
    }
    auto req = ready.back();
    ready.pop_back();
    return req;
}
//---------------------------------------------------------------------------
void PollSocket::enqueue(int fd, short events, RequestInfo req)
// Implement the fd into our submission queue
{
    pollfds.emplace_back(fd, events);
    fdToRequest.emplace(fd, req);
    ++submitted;
}
//---------------------------------------------------------------------------
int32_t PollSocket::submit()
// Submit requests
{
    auto sub = submitted;
    submitted = 0;
    return sub;
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
