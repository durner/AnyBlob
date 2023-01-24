#include "network/tasked_send_receiver.hpp"
#include "network/http_helper.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <unistd.h>
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
using namespace std;
//---------------------------------------------------------------------------
TaskedSendReceiverGroup::TaskedSendReceiverGroup(uint64_t concurrentRequests, uint64_t submissions, uint64_t chunkSize, uint64_t completions, uint64_t reuse) : _submissions(submissions), _completions(!completions ? 0.2 * submissions : completions), _reuse(!reuse ? submissions : reuse), _sendReceivers(), _resizeMutex(), _head(nullptr), _chunkSize(chunkSize), _concurrentRequests(concurrentRequests), _cv(), _mutex()
// Initializes the global submissions and completions
{
}
//---------------------------------------------------------------------------
TaskedSendReceiverGroup::~TaskedSendReceiverGroup()
/// The destructor
{
    while (auto val = _reuse.consume<false>()) {
        if (val.has_value())
            delete val.value();
    }
}
//---------------------------------------------------------------------------
bool TaskedSendReceiverGroup::send(OriginalMessage* msg)
/// Adds a message to the submission queue
{
    return (_submissions.insert<false>(msg) != ~0ull);
}
//---------------------------------------------------------------------------
bool TaskedSendReceiverGroup::send(span<OriginalMessage*> msgs)
/// Adds a message to the submission queue
{
    return (_submissions.insert<false>(msgs) != ~0ull);
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> TaskedSendReceiverGroup::receive(OriginalMessage* msg, bool async)
/// Receives the messages and blocks until it is received
{
    do {
        auto it = _completions.find(reinterpret_cast<uintptr_t>(msg));
        if (it != _completions.end()) {
            auto res = move(it->second);
            _completions.erase(it);
            return res;
        } else {
            if (async)
                return nullptr;
            unique_lock<mutex> lock(_mutex);
            _cv.wait(lock);
        }
    } while (true);
}
//---------------------------------------------------------------------------
TaskSendReceiverHandle TaskedSendReceiverGroup::getHandle()
/// Runs a new tasked send receiver deamon
{
    for (auto head = _head.load(); head;) {
        if (_head.compare_exchange_weak(head, head->_next)) {
            TaskSendReceiverHandle handle(this);
            handle._sendReceiver = head;
            return handle;
        }
        head = _head;
    }
    lock_guard<mutex> lg(_resizeMutex);
    auto& ref = _sendReceivers.emplace_back(make_unique<TaskedSendReceiver>(*this));
    TaskSendReceiverHandle handle(this);
    handle._sendReceiver = ref.get();
    return handle;
}
//---------------------------------------------------------------------------
void TaskedSendReceiverGroup::sendReceive(bool oneQueueInvocation)
/// Creates a sending message with chaining IOSQE_IO_LINK, creates a receiving message, submits queue, and waits for result
{
    auto handle = getHandle();
    handle.sendReceive(oneQueueInvocation);
}
//---------------------------------------------------------------------------
TaskSendReceiverHandle::TaskSendReceiverHandle(TaskedSendReceiverGroup* group) : _group(group)
/// The constructor
{
}
//---------------------------------------------------------------------------
TaskSendReceiverHandle::TaskSendReceiverHandle(TaskSendReceiverHandle&& other)
/// Move constructor
{
    _group = other._group;
    _sendReceiver = other._sendReceiver;
    other._sendReceiver = nullptr;
}
//---------------------------------------------------------------------------
TaskSendReceiverHandle& TaskSendReceiverHandle::operator=(TaskSendReceiverHandle&& other)
/// Move assignment
{
    if (other._group == _group) {
        _sendReceiver = other._sendReceiver;
        other._sendReceiver = nullptr;
    }
    return *this;
}
//---------------------------------------------------------------------------
bool TaskSendReceiverHandle::run()
/// Starts the deamon
{
    if (!_sendReceiver)
        return false;
    _sendReceiver->run();
    return true;
}
//---------------------------------------------------------------------------
void TaskSendReceiverHandle::stop()
/// Stops the deamon
{
    if (!_sendReceiver)
        return;
    _sendReceiver->stop();
    auto ptr = _sendReceiver;
    for (auto head = _group->_head.load();;) {
        ptr->_next = head;
        if (_group->_head.compare_exchange_weak(head, ptr)) {
            return;
        }
        head = _group->_head;
    }
}
//---------------------------------------------------------------------------
bool TaskSendReceiverHandle::sendReceive(bool oneQueueInvocation)
/// Creates a sending message with chaining IOSQE_IO_LINK, creates a receiving message, submits queue, and waits for result
{
    if (!_sendReceiver)
        return false;
    _sendReceiver->sendReceive(oneQueueInvocation);
    return true;
}
//---------------------------------------------------------------------------
TaskedSendReceiver::TaskedSendReceiver(TaskedSendReceiverGroup& group) : _group(group), _next(nullptr), _socketWrapper(make_unique<IOUringSocket>(group._concurrentRequests << 2)), _messageTasks(), _timings(nullptr), _stopDeamon(false)
/// The constructor
{
}
//---------------------------------------------------------------------------
void TaskedSendReceiver::run()
/// Deamon
{
    try {
        sendReceive(false);
    } catch (exception& e) {
        cerr << e.what() << endl;
    };
}
//---------------------------------------------------------------------------
int32_t TaskedSendReceiver::connect(string hostname, uint32_t port)
/// Connect to the socket
{
    IOUringSocket::TCPSettings tcpSettings;
    return _socketWrapper->connect(hostname, port, tcpSettings);
}
//---------------------------------------------------------------------------
bool TaskedSendReceiver::send(OriginalMessage* msg)
/// Adds a message to the submission queue
{
    return _group._submissions.insert<false>(msg);
}
//---------------------------------------------------------------------------
void TaskedSendReceiver::reuse(unique_ptr<utils::DataVector<uint8_t>> message)
/// Reuse message
{
    _group._reuse.insert<false>(message.release());
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> TaskedSendReceiver::receive(OriginalMessage* msg)
/// Receives the messages and blocks until it is received
{
    return _group.receive(msg, false);
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> TaskedSendReceiver::receiveAsync(OriginalMessage* msg)
/// Receives the messages if available else nullptr
{
    return _group.receive(msg, true);
}
//--------------------------------------------------------------------------
void TaskedSendReceiver::sendReceive(bool oneQueueInvocation)
/// Creates a sending message chaining IOSQE_IO_LINK, creates a receiving message, and waits for result
{
    // Reset the stop
    _stopDeamon = false;

    // Current requests in flight
    auto submissions = 0u;

    // Send new requests
    auto emplaceNewRequest = [&] {
        while (auto val = _group._submissions.consume<false>()) {
            /// Create new submission requests if within concurrency
            if (!val.has_value())
                break;

            auto original = val.value();
            _messageTasks.emplace_back(make_unique<HTTPMessage>(original, _group._chunkSize, original->receiveBuffer, original->bufferSize));

            if (!original->result) {
                if (auto mem = _group._reuse.consume<false>()) {
                    if (mem.has_value()) {
                        original->result = unique_ptr<utils::DataVector<uint8_t>>(mem.value());
                    }
                }
            }

            if (_timings)
                (*_timings)[original->traceId].start = chrono::steady_clock::now();

            // Start the message process
            _messageTasks.back()->execute(*_socketWrapper);

            if (_messageTasks.size() >= _group._concurrentRequests)
                break;
        }
    };

    // iterate over all messages
    while (!_stopDeamon || submissions) {
        if (submissions > 0) {
            // get cqe
            auto req = _socketWrapper->complete();
            submissions--;
            // get the task
            auto task = reinterpret_cast<MessageTask*>(req->messageTask);
            // execute next task
            auto status = task->execute(*_socketWrapper);
            // check if finished
            if (status == MessageState::Finished) {
                for (auto it = _messageTasks.begin(); it != _messageTasks.end(); it++) {
                    if (it->get() == task) {
                        // Remove the second param with the real data
                        if (_timings) {
                            auto sv = HTTPHelper::retrieveContent(task->originalMessage->result->data(), task->originalMessage->result->size(), static_cast<HTTPMessage*>(it->get())->info);
                            (*_timings)[task->originalMessage->traceId].size = sv.size();
                            (*_timings)[task->originalMessage->traceId].finish = chrono::steady_clock::now();
                        }

                        if (!task->originalMessage->requiresFinish())
                            _group._completions.push(reinterpret_cast<uintptr_t>(task->originalMessage), move(task->originalMessage->result));
                        else
                            task->originalMessage->finish();
                        _messageTasks.erase(it);
                        _group._cv.notify_all();
                        break;
                    }
                }
            } else if (_timings && status == MessageState::Receiving && !task->receiveBufferOffset) {
                (*_timings)[task->originalMessage->traceId].recieve = chrono::steady_clock::now();
            }
        }
        if (!_stopDeamon && _messageTasks.size() < _group._concurrentRequests) {
            emplaceNewRequest();
        }
        if (submissions <= _messageTasks.size() / 2)
            submissions += _socketWrapper->submit();

        if (oneQueueInvocation && _group._submissions.empty() && !submissions) {
            _stopDeamon = true;
            continue;
        }
    }
    if (oneQueueInvocation)
        _stopDeamon = false;
}
//---------------------------------------------------------------------------
int32_t TaskedSendReceiver::submitRequests()
/// Submits the queue
{
    return _socketWrapper->submit();
}
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
