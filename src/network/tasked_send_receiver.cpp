#include "network/tasked_send_receiver.hpp"
#include "network/http_message.hpp"
#include "network/https_message.hpp"
#include "network/message_result.hpp"
#include "network/original_message.hpp"
#include "network/tls_context.hpp"
#include "utils/data_vector.hpp"
#include "utils/timer.hpp"
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
#ifndef NDEBUG
#define verify(expression) assert(expression)
#else
#define verify(expression) ((void) (expression))
#endif
//---------------------------------------------------------------------------
namespace anyblob {
namespace network {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
TaskedSendReceiverGroup::TaskedSendReceiverGroup(uint64_t concurrentRequests, uint64_t submissions, uint64_t chunkSize, uint64_t reuse) : _submissions(submissions), _reuse(!reuse ? submissions : reuse), _sendReceivers(), _resizeMutex(), _head(nullptr), _chunkSize(chunkSize), _concurrentRequests(concurrentRequests), _cv(), _mutex()
// Initializes the global submissions and completions
{
    context = make_unique<TLSContext>();
}
//---------------------------------------------------------------------------
TaskedSendReceiverGroup::~TaskedSendReceiverGroup()
// The destructor
{
    while (auto val = _reuse.consume()) {
        if (val.has_value())
            delete val.value();
    }
}
//---------------------------------------------------------------------------
bool TaskedSendReceiverGroup::send(OriginalMessage* msg)
// Adds a message to the submission queue
{
    return (_submissions.insert(msg) != ~0ull);
}
//---------------------------------------------------------------------------
bool TaskedSendReceiverGroup::send(span<OriginalMessage*> msgs)
// Adds a message to the submission queue
{
    return (_submissions.insertAll(msgs) != ~0ull);
}
//---------------------------------------------------------------------------
TaskSendReceiverHandle TaskedSendReceiverGroup::getHandle()
// Runs a new tasked send receiver deamon
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
void TaskedSendReceiverGroup::process(bool oneQueueInvocation)
// Submits queue and waits for result
{
    auto handle = getHandle();
    handle.sendReceive(oneQueueInvocation);
}
//---------------------------------------------------------------------------
TaskSendReceiverHandle::TaskSendReceiverHandle(TaskedSendReceiverGroup* group) : _group(group)
// The constructor
{
}
//---------------------------------------------------------------------------
TaskSendReceiverHandle::TaskSendReceiverHandle(TaskSendReceiverHandle&& other)
// Move constructor
{
    _group = other._group;
    _sendReceiver = other._sendReceiver;
    other._sendReceiver = nullptr;
}
//---------------------------------------------------------------------------
TaskSendReceiverHandle& TaskSendReceiverHandle::operator=(TaskSendReceiverHandle&& other)
// Move assignment
{
    if (other._group == _group) {
        _sendReceiver = other._sendReceiver;
        other._sendReceiver = nullptr;
    }
    return *this;
}
//---------------------------------------------------------------------------
bool TaskSendReceiverHandle::run()
// Starts the deamon
{
    if (!_sendReceiver)
        return false;
    _sendReceiver->run();
    return true;
}
//---------------------------------------------------------------------------
void TaskSendReceiverHandle::stop()
// Stops the deamon
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
// Creates a sending message with chaining IOSQE_IO_LINK, creates a receiving message, submits queue, and waits for result
{
    if (!_sendReceiver)
        return false;
    _sendReceiver->sendReceive(false, oneQueueInvocation);
    return true;
}
//---------------------------------------------------------------------------
TaskedSendReceiver::TaskedSendReceiver(TaskedSendReceiverGroup& group) : _group(group), _submissions(), _next(nullptr), _socketWrapper(make_unique<IOUringSocket>(group._concurrentRequests << 2)), _messageTasks(), _timings(nullptr), _stopDeamon(false)
// The constructor
{
}
//---------------------------------------------------------------------------
void TaskedSendReceiver::run()
// Deamon
{
    try {
        sendReceive(false);
    } catch (exception& e) {
        cerr << e.what() << endl;
    };
}
//---------------------------------------------------------------------------
int32_t TaskedSendReceiver::connect(string hostname, uint32_t port)
// Connect to the socket
{
    IOUringSocket::TCPSettings tcpSettings;
    return _socketWrapper->connect(hostname, port, tcpSettings);
}
//---------------------------------------------------------------------------
bool TaskedSendReceiver::send(OriginalMessage* msg)
// Adds a message to the group submission queue
{
    return (_group._submissions.insert(msg) != ~0ull);
}
//---------------------------------------------------------------------------
void TaskedSendReceiver::sendSync(OriginalMessage* msg)
// Adds a message to the local submission queue
{
    _submissions.emplace(msg);
}
//---------------------------------------------------------------------------
void TaskedSendReceiver::reuse(unique_ptr<utils::DataVector<uint8_t>> message)
// Reuse message
{
    verify(_group._reuse.insert(message.release()) != ~0ull);
}
//--------------------------------------------------------------------------
void TaskedSendReceiver::addResolver(const std::string& hostname, std::unique_ptr<Resolver> resolver)
// Adds a resolver
{
    _socketWrapper->addResolver(hostname, move(resolver));
}
//--------------------------------------------------------------------------
void TaskedSendReceiver::sendReceive(bool local, bool oneQueueInvocation)
// Creates a sending message chaining IOSQE_IO_LINK, creates a receiving message, and waits for result
{
    // Reset the stop
    _stopDeamon = false;

    // Current requests in flight
    auto count = 0u;

    // Send new requests
    auto emplaceNewRequest = [&] {
        while (auto val = _group._submissions.consume()) {
            // Create new submission requests if within concurrency
            if (!val.has_value())
                break;

            auto original = val.value();
            auto messageTask = MessageTask::buildMessageTask(original, *_group.context, _group._chunkSize);
            assert(messageTask.get());

            if (!original->result.getDataVector().capacity()) {
                auto mem = _group._reuse.consume();
                if (mem.has_value())
                    original->setResultVector(mem.value());
            }

            if (_timings)
                (*_timings)[original->traceId].start = chrono::steady_clock::now();

            // Start the message process
            if (messageTask->execute(*_socketWrapper) == MessageState::Aborted) {
                if (messageTask->originalMessage->requiresFinish())
                    messageTask->originalMessage->finish();
                continue;
            }
            // Insert into the task vector
            _messageTasks.emplace_back(move(messageTask));

            if (_messageTasks.size() >= _group._concurrentRequests)
                break;
        }
    };

    // Send new requests
    auto emplaceLocalRequest = [&] {
        while (!_submissions.empty()) {
            OriginalMessage* original = _submissions.front();
            _submissions.pop();
            auto messageTask = MessageTask::buildMessageTask(original, *_group.context, _group._chunkSize);
            assert(messageTask.get());

            if (!original->result.getDataVector().capacity()) {
                auto mem = _group._reuse.consume();
                if (mem.has_value())
                    original->setResultVector(mem.value());
            }

            if (_timings)
                (*_timings)[original->traceId].start = chrono::steady_clock::now();

            // Start the message process
            if (messageTask->execute(*_socketWrapper) == MessageState::Aborted) {
                if (messageTask->originalMessage->requiresFinish())
                    messageTask->originalMessage->finish();
                continue;
            }
            // Insert into the task vector
            _messageTasks.emplace_back(move(messageTask));

            if (_messageTasks.size() >= _group._concurrentRequests)
                break;
        }
    };

    // iterate over all messages
    while (!_stopDeamon || count) {
        if (count > 0) {
            // get cqe
            IOUringSocket::Request* req;
            try {
                req = _socketWrapper->complete();
            } catch (exception& e) {
                continue;
            }
            // reduce count
            count--;

            // nullptr in user_data if IORING_OP_LINK_TIMEOUT, skip this one
            if (!req)
                continue;

            // get the task
            auto task = reinterpret_cast<MessageTask*>(req->messageTask);
            // execute next task
            auto status = task->execute(*_socketWrapper);
            // check if finished
            if (status == MessageState::Finished || status == MessageState::Aborted) {
                for (auto it = _messageTasks.begin(); it != _messageTasks.end(); it++) {
                    if (it->get() == task) {
                        // Remove the second param with the real data
                        if (_timings) {
                            (*_timings)[task->originalMessage->traceId].size = task->originalMessage->result.getSize();
                            (*_timings)[task->originalMessage->traceId].finish = chrono::steady_clock::now();
                        }

                        if (task->originalMessage->requiresFinish())
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
            local ? emplaceLocalRequest() : emplaceNewRequest();
        }
        count += _socketWrapper->submit();

        auto empty = local ? _submissions.empty() : _group._submissions.empty();
        if (oneQueueInvocation && empty && !count) {
            _stopDeamon = true;
            continue;
        }
    }
    if (oneQueueInvocation)
        _stopDeamon = false;
}
//---------------------------------------------------------------------------
int32_t TaskedSendReceiver::submitRequests()
// Submits the queue
{
    return _socketWrapper->submit();
}
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
