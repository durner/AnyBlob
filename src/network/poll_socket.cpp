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
bool PollSocket::send(const Request& req, int32_t msg_flags)
// Prepare a submission send with timeout
{
    if (req.event != EventType::write) return false;
#ifdef ANYBLOB_HAS_IO_URING
    auto timeout = req.timeout.has_value() ? chrono::steady_clock::now() + chrono::seconds(req.timeout->tv_sec) + chrono::nanoseconds(req.timeout->tv_nsec) : chrono::time_point<chrono::steady_clock>::max();
#else
    auto timeout = req.timeout.has_value() ? chrono::steady_clock::now() + *req.timeout : chrono::time_point<chrono::steady_clock>::max();
#endif
    enqueue(req.fd, POLLOUT, RequestInfo{.request = const_cast<Request*>(&req), .timeout = timeout, .flags = msg_flags});
    return true;
}
//---------------------------------------------------------------------------
bool PollSocket::recv(Request& req, int32_t msg_flags)
// Prepare a submission recv with timeout
{
    if (req.event != EventType::read) return false;
#ifdef ANYBLOB_HAS_IO_URING
    auto timeout = req.timeout.has_value() ? chrono::steady_clock::now() + chrono::seconds(req.timeout->tv_sec) + chrono::nanoseconds(req.timeout->tv_nsec) : chrono::time_point<chrono::steady_clock>::max();
#else
    auto timeout = req.timeout.has_value() ? chrono::steady_clock::now() + *req.timeout : chrono::time_point<chrono::steady_clock>::max();
#endif
    enqueue(req.fd, POLLIN, RequestInfo{.request = &req, .timeout = timeout, .flags = msg_flags});
    return true;
}
//---------------------------------------------------------------------------
PollSocket::Request* PollSocket::complete()
// Get a completion event and mark it as seen; return the Request
{
    while (ready.empty()) {
        // Activly poll here as well to match io_uring because we have to check for new arrivals
        if (readyFds <= 0) {
            // Poll wait up to 1ms
            readyFds = ::poll(pollfds.data(), pollfds.size(), 1);
            // No events ready
            if (readyFds <= 0) continue;
        }

        // Check for completed events
        auto currentTime = chrono::steady_clock::now();
        for (auto pit = pollfds.begin(); pit != pollfds.end();) {
            if (auto it = fdToRequest.find(pit->fd); it != fdToRequest.end()) {
                auto& req = it->second;
                if (pit->revents & (POLLIN | POLLOUT)) {
                    // Active pollfd
                    if (req.request->event == EventType::read) {
                        req.request->length = ::recv(it->first, req.request->data.data, static_cast<size_t>(req.request->length), req.flags | MSG_DONTWAIT);
                    } else if (req.request->event == EventType::write) {
                        req.request->length = ::send(it->first, req.request->data.cdata, static_cast<size_t>(req.request->length), req.flags | MSG_DONTWAIT);
                    }

                    // Simulate io uring by returning -errno
                    if (req.request->length == -1)
                        req.request->length = -errno;

                    ready.push_back(req.request);
                    fdToRequest.erase(it->first);
                    pit = pollfds.erase(pit);
                } else if (pit->revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    // Simulate io uring by returning -error
                    int err = 0;
                    socklen_t len = sizeof(err);
                    if (::getsockopt(it->first, SOL_SOCKET, SO_ERROR, &err, &len) == 0) {
                        req.request->length = -err;
                    } else {
                        req.request->length = -EIO;
                    }
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
    // Do the poll here, but don't wait or work on the results
    readyFds = ::poll(pollfds.data(), pollfds.size(), 0);
    return sub;
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
