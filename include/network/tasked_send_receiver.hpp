#pragma once
#include "network/io_uring_socket.hpp"
#include "network/messages.hpp"
#include "utils/data_vector.hpp"
#include "utils/ring_buffer.hpp"
#include "utils/timer.hpp"
#include "utils/unordered_map.hpp"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>
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
class TaskedSendReceiver;
class TaskSendReceiverHandle;
//---------------------------------------------------------------------------
/// Shared submission and completions queue with multiple TaskedSendReceivers
class TaskedSendReceiverGroup {
    /// The global submission ringbuffer
    utils::RingBuffer<network::OriginalMessage*> _submissions;
    /// The global completion map
    utils::UnorderedMap<uintptr_t, std::unique_ptr<utils::DataVector<uint8_t>>> _completions;
    /// Reuse elements
    utils::RingBuffer<utils::DataVector<uint8_t>*> _reuse;

    /// The send receivers
    std::vector<std::unique_ptr<TaskedSendReceiver>> _sendReceivers;
    /// Resize the vector
    std::mutex _resizeMutex;

    /// Implicitly handle the unused send receivers
    std::atomic<TaskedSendReceiver*> _head;

    /// The recv chunk size
    uint64_t _chunkSize;
    /// The queue maximum for each TaskedSendReceiver
    uint64_t _concurrentRequests;

    /// Condition variable to stop wasting wait cycles
    std::condition_variable _cv;
    /// Mutex for condition variable
    std::mutex _mutex;

    public:
    /// Initializes the global submissions and completions
    TaskedSendReceiverGroup(uint64_t concurrentRequests, uint64_t submissions, uint64_t chunkSize = 64u * 1024, uint64_t completions = 0, uint64_t reuse = 0);
    /// Destructor
    ~TaskedSendReceiverGroup();

    /// Adds a message to the submission queue
    bool send(OriginalMessage* msg);
    /// Adds a span of message to the submission queue
    bool send(std::span<OriginalMessage*> msgs);
    /// Receive a message
    std::unique_ptr<utils::DataVector<uint8_t>> receive(OriginalMessage* msg, bool async = true);
    /// Gets a tasked send receiver deamon
    TaskSendReceiverHandle getHandle();
    /// Creates a sending message with chaining IOSQE_IO_LINK, creates a receiving message, submits queue, and waits for result
    void sendReceive(bool oneQueueInvocation = true);

    friend TaskedSendReceiver;
    friend TaskSendReceiverHandle;
};
//---------------------------------------------------------------------------
/// Implements a send recieve roundtrip with the help of IOUringSockets
/// TaskedSendReceiver uses one socket and can contain mutliple requests
class TaskedSendReceiver {
    private:
    /// The shared group
    TaskedSendReceiverGroup& _group;

    /// Implicitly handle the unused send receivers
    std::atomic<TaskedSendReceiver*> _next;

    /// The socket wrapper
    std::unique_ptr<IOUringSocket> _socketWrapper;
    /// The current tasks
    std::vector<std::unique_ptr<MessageTask>> _messageTasks;
    /// Timing infos
    std::vector<utils::TimingHelper>* _timings;
    /// Stops the daemon
    std::atomic<bool> _stopDeamon;

    public:
    /// The constructor
    TaskedSendReceiver(TaskedSendReceiverGroup& group);
    /// Creates a sending message with chaining IOSQE_IO_LINK, creates a receiving message, submits queue, and waits for result
    void sendReceive(bool oneQueueInvocation = true);
    /// Adds a message to the submission queue
    bool send(OriginalMessage* msg);
    /// Receives the messages and blocks until it is received
    std::unique_ptr<utils::DataVector<uint8_t>> receive(OriginalMessage* msg);
    /// Receives the messages if available else nullptr
    std::unique_ptr<utils::DataVector<uint8_t>> receiveAsync(OriginalMessage* msg);
    /// Reuse memory of old message
    void reuse(std::unique_ptr<utils::DataVector<uint8_t>> message);
    /// Runs the deamon
    void run();
    /// Stops the deamon
    void stop() { _stopDeamon = true; }
    /// Update the concurrent requests
    void setConcurrentRequests(uint64_t concurrentRequests) {
        if (_group._concurrentRequests != concurrentRequests)
            _group._concurrentRequests = concurrentRequests;
    }
    /// Get the concurrent requests
    uint64_t getConcurrentRequests() {
        return _group._concurrentRequests;
    }
    /// Set the timings
    void setTimings(std::vector<utils::TimingHelper>* timings) {
        _timings = timings;
    }
    /// Adds a resolver
    void addResolver(const std::string& hostname, std::unique_ptr<Resolver> resolver) {
        _socketWrapper->addResolver(hostname, move(resolver));
    }

    private:
    /// Connect to the socket return fd
    int32_t connect(std::string hostname, uint32_t port);
    /// Submits the queue
    int32_t submitRequests();

    friend TaskedSendReceiverGroup;
    friend TaskSendReceiverHandle;
};
//---------------------------------------------------------------------------
/// Handle to a TaskedSendReceiver
class TaskSendReceiverHandle {
    private:
    /// The shared group
    TaskedSendReceiverGroup* _group;
    /// The send receiver
    TaskedSendReceiver* _sendReceiver;

    /// Default constructor is deleted
    TaskSendReceiverHandle() = delete;
    /// Consturctor
    TaskSendReceiverHandle(TaskedSendReceiverGroup* group);
    /// Delete copy
    TaskSendReceiverHandle(TaskSendReceiverHandle& other) = delete;
    /// Delete copy assignment
    TaskSendReceiverHandle& operator=(TaskSendReceiverHandle& other) = delete;

    /// Creates a sending message with chaining IOSQE_IO_LINK, creates a receiving message, submits queue, and waits for result
    bool sendReceive(bool oneQueueInvocation = true);

    public:
    /// Move constructor
    TaskSendReceiverHandle(TaskSendReceiverHandle&& other);
    /// Move assignment
    TaskSendReceiverHandle& operator=(TaskSendReceiverHandle&& other);

    /// Runs the handle, thread becomes the the TaskedSendReceiver
    bool run();
    /// Stops the handle thread
    void stop();

    friend TaskedSendReceiverGroup;
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
