#include "network/poll_socket.hpp"
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
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
static chrono::steady_clock::time_point deadline(chrono::milliseconds timeout)
// Compute the deadline of a request
{
    return timeout.count() ? chrono::steady_clock::now() + timeout : chrono::steady_clock::time_point::max();
}
//---------------------------------------------------------------------------
void PollSocket::prepare(Request& req, chrono::milliseconds timeout, int32_t msg_flags)
// Prepare a submission
{
    auto write = req.event == EventType::write;
    enqueue(req.fd, write ? POLLOUT : POLLIN, RequestInfo{.request = &req, .timeout = deadline(timeout), .flags = msg_flags});
}
//---------------------------------------------------------------------------
void PollSocket::prepareTimer(Request& req, chrono::milliseconds delay)
// Prepare a timer submission
{
    req.event = EventType::timer;
    timers.push_back(RequestInfo{.request = &req, .timeout = deadline(delay), .flags = 0});
}
//---------------------------------------------------------------------------
bool PollSocket::collectReady()
// Poll the registered fds and append the finished tasks
{
    if (::poll(pollfds.data(), pollfds.size(), 1) < 0) {
        if (errno == EINTR)
            return false;
        throw runtime_error("poll error! " + string(strerror(errno)));
    }

    auto completed = false;
    auto currentTime = chrono::steady_clock::now();
    for (auto tit = timers.begin(); tit != timers.end();) {
        if (tit->timeout < currentTime) {
            tit->request->length = -ETIME;
            _completions.push_back(tit->request->messageTask);
            tit = timers.erase(tit);
            completed = true;
        } else {
            ++tit;
        }
    }
    for (auto pit = pollfds.begin(); pit != pollfds.end();) {
        if (auto it = fdToRequest.find(pit->fd); it != fdToRequest.end()) {
            auto& req = it->second;
            auto finish = [&](int64_t result) {
                req.request->length = result;
                _completions.push_back(req.request->messageTask);
                fdToRequest.erase(it);
                pit = pollfds.erase(pit);
                completed = true;
            };
            if (pit->revents & (POLLIN | POLLOUT)) {
                int64_t result = 0;
                if (req.request->event == EventType::read) {
                    result = ::recv(it->first, req.request->data.data, static_cast<size_t>(req.request->length), req.flags | MSG_DONTWAIT);
                } else if (req.request->event == EventType::write) {
                    result = ::send(it->first, req.request->data.cdata, static_cast<size_t>(req.request->length), req.flags | MSG_DONTWAIT | MSG_NOSIGNAL);
                }
                finish(result == -1 ? -errno : result);
            } else if (pit->revents & (POLLERR | POLLHUP | POLLNVAL)) {
                int err = 0;
                socklen_t len = sizeof(err);
                finish(::getsockopt(it->first, SOL_SOCKET, SO_ERROR, &err, &len) == 0 ? -err : -EIO);
            } else if (req.timeout < currentTime) {
                finish(-ETIMEDOUT);
            } else {
                ++pit;
            }
        } else {
            throw runtime_error("couldn't find request");
        }
    }
    return completed;
}
//---------------------------------------------------------------------------
void PollSocket::processImpl()
// Submit the queued requests and append the completed tasks
{
    if (hasOutstanding())
        while (!collectReady());
}
//---------------------------------------------------------------------------
void PollSocket::enqueue(int fd, short events, RequestInfo req)
// Implement the fd into our submission queue
{
    if (!fdToRequest.emplace(fd, req).second)
        throw runtime_error("the fd already has an outstanding request");
    pollfds.emplace_back(fd, events);
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
