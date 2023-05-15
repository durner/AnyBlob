#pragma once
#include "network/io_uring_socket.hpp"
#include "network/message_task.hpp"
#include "network/tls_context.hpp"
#include "utils/ring_buffer.hpp"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <span>
#include <vector>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace utils {
//---------------------------------------------------------------------------
template <typename T>
class DataVector;
struct TimingHelper;
} // namespace utils
//---------------------------------------------------------------------------
namespace network {
//---------------------------------------------------------------------------
class TaskedSendReceiver;
class TaskSendReceiverHandle;
class Resolver;
struct OriginalMessage;
//---------------------------------------------------------------------------
/// Shared submission and completions queue with multiple TaskedSendReceivers
class TaskedSendReceiverGroup {
    /// The global submission ringbuffer
    utils::RingBuffer<network::OriginalMessage*> _submissions;
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
    TaskedSendReceiverGroup(uint64_t concurrentRequests, uint64_t submissions, uint64_t chunkSize = 64u * 1024, uint64_t reuse = 0);
    /// Destructor
    ~TaskedSendReceiverGroup();

    /// Adds a message to the submission queue
    [[nodiscard]] bool send(OriginalMessage* msg);
    /// Adds a span of message to the submission queue
    [[nodiscard]] bool send(std::span<OriginalMessage*> msgs);
    /// Gets a tasked send receiver deamon
    [[nodiscard]] TaskSendReceiverHandle getHandle();
    /// Submits group queue and waits for result
    void process(bool oneQueueInvocation = true);

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
    /// The local message group
    std::queue<OriginalMessage*> _submissions;

    /// Implicitly handle the unused send receivers
    std::atomic<TaskedSendReceiver*> _next;

    /// The socket wrapper
    std::unique_ptr<IOUringSocket> _socketWrapper;
    /// The current tasks
    std::vector<std::unique_ptr<MessageTask>> _messageTasks;
    /// The tls context
    std::unique_ptr<TLSContext> _context;
    /// Timing infos
    std::vector<utils::TimingHelper>* _timings;
    /// Stops the daemon
    std::atomic<bool> _stopDeamon;

    public:
    /// The constructor
    TaskedSendReceiver(TaskedSendReceiverGroup& group);

    /// Get the group
    [[nodiscard]] const TaskedSendReceiverGroup* getGroup() const { return &_group; }

    /// Adds a resolver
    void addResolver(const std::string& hostname, std::unique_ptr<Resolver> resolver);

    /// Adds a message to the submission queue
    bool send(OriginalMessage* msg);
    /// Adds a message to the submission queue
    void sendSync(OriginalMessage* msg);

    /// Process group submissions (should be used for async requests)
    inline void process(bool oneQueueInvocation = true) { sendReceive(false, oneQueueInvocation); }
    /// Process local submissions (should be used for sync requests)
    inline void processSync(bool oneQueueInvocation = true) { sendReceive(true, oneQueueInvocation); }

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
    /// Reuse memory of old message
    void reuse(std::unique_ptr<utils::DataVector<uint8_t>> message);
    /// Adds a resolver

    private:
    /// Submits queue and waits for result
    void sendReceive(bool local = false, bool oneQueueInvocation = true);
    /// Connect to the socket return fd
    [[nodiscard]] int32_t connect(std::string hostname, uint32_t port);
    /// Submits the queue
    [[nodiscard]] int32_t submitRequests();

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

    /// Submits queue and waits for result
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
