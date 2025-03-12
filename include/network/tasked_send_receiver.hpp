#pragma once
#include "network/config.hpp"
#include "network/connection_manager.hpp"
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
#include <thread>
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
class TaskedSendReceiverHandle;
class Cache;
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
    utils::RingBuffer<TaskedSendReceiver*> _sendReceiverCache;

    /// The number of submissions in queue (per thread)
    static constexpr uint64_t submissionPerCore = 1 << 10;
    /// The recv chunk size
    uint64_t _chunkSize;
    /// The queue maximum for each TaskedSendReceiver
    unsigned _concurrentRequests;
    /// The TCP settings
    std::unique_ptr<ConnectionManager::TCPSettings> _tcpSettings;

    /// Condition variable to stop wasting wait cycles
    std::condition_variable _cv;
    /// Mutex for condition variable
    std::mutex _mutex;

    public:
    /// Initializes the global submissions and completions
    explicit TaskedSendReceiverGroup(unsigned chunkSize = 64u * 1024, uint64_t submissions = std::thread::hardware_concurrency() * submissionPerCore, uint64_t reuse = 0);
    /// Destructor
    ~TaskedSendReceiverGroup();

    /// Adds a message to the submission queue
    [[nodiscard]] bool send(OriginalMessage* msg);
    /// Adds a span of message to the submission queue
    [[nodiscard]] bool send(std::span<OriginalMessage*> msgs);
    /// Gets a tasked send receiver deamon
    [[nodiscard]] TaskedSendReceiverHandle getHandle();
    /// Submits group queue and waits for result
    void process(bool oneQueueInvocation = true);

    /// Update the concurrent requests via config
    void setConfig(const network::Config& config) {
        if (_concurrentRequests != config.coreRequests())
            _concurrentRequests = config.coreRequests();
    }
    /// Update the concurrent requests
    void setConcurrentRequests(unsigned concurrentRequests) {
        if (_concurrentRequests != concurrentRequests)
            _concurrentRequests = concurrentRequests;
    }
    /// Get the concurrent requests
    unsigned getConcurrentRequests() const {
        return _concurrentRequests;
    }

    friend TaskedSendReceiver;
    friend TaskedSendReceiverHandle;
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

    /// The connection manager
    std::unique_ptr<ConnectionManager> _connectionManager;
    /// The current tasks
    std::vector<std::unique_ptr<MessageTask>> _messageTasks;
    /// Timing infos
    std::vector<utils::TimingHelper>* _timings;
    /// Stops the daemon
    std::atomic<bool> _stopDeamon;
#ifndef NDEBUG
    /// Count the threads
    std::atomic<uint64_t> countThreads = 0;
#endif

    public:
    /// Get the group
    [[nodiscard]] const TaskedSendReceiverGroup* getGroup() const { return &_group; }
    /// Adds a domain-specific cache
    void addCache(const std::string& hostname, std::unique_ptr<Cache> cache);
    /// Process local submissions (should be used for sync requests)
    inline void processSync(bool oneQueueInvocation = true) { sendReceive(true, oneQueueInvocation); }
    /// Set the timings
    void setTimings(std::vector<utils::TimingHelper>* timings) {
        _timings = timings;
    }
    /// Reuse memory of old message
    void reuse(std::unique_ptr<utils::DataVector<uint8_t>> message);
    /// Get reused memory
    std::unique_ptr<utils::DataVector<uint8_t>> getReused();

    private:
    /// Default constructor is deleted
    TaskedSendReceiver() = delete;
    /// Delete copy
    TaskedSendReceiver(TaskedSendReceiver& other) = delete;
    /// Delete copy assignment
    TaskedSendReceiver& operator=(TaskedSendReceiver& other) = delete;
    /// The constructor
    explicit TaskedSendReceiver(TaskedSendReceiverGroup& group);

    /// Adds a message to the submission queue
    void sendSync(OriginalMessage* msg);
    /// Stops the deamon
    void stop() { _stopDeamon = true; }
    /// Submits queue and waits for result
    void sendReceive(bool local = false, bool oneQueueInvocation = true);
    /// Submits the queue
    [[nodiscard]] int32_t submitRequests();
    /// Reset the receiver
    void reset();

    friend TaskedSendReceiverGroup;
    friend TaskedSendReceiverHandle;
};
//---------------------------------------------------------------------------
/// Handle to a TaskedSendReceiver
class TaskedSendReceiverHandle {
    private:
    /// The shared group
    TaskedSendReceiverGroup* _group;
    /// The send receiver
    TaskedSendReceiver* _sendReceiver;

    /// Default constructor is deleted
    TaskedSendReceiverHandle() = delete;
    /// Consturctor
    explicit TaskedSendReceiverHandle(TaskedSendReceiverGroup* group);
    /// Delete copy
    TaskedSendReceiverHandle(TaskedSendReceiverHandle& other) = delete;
    /// Delete copy assignment
    TaskedSendReceiverHandle& operator=(TaskedSendReceiverHandle& other) = delete;

    /// Submits queue and waits for result
    bool sendReceive(bool local, bool oneQueueInvocation = true);

    public:
    /// Move constructor
    TaskedSendReceiverHandle(TaskedSendReceiverHandle&& other);
    /// Move assignment
    TaskedSendReceiverHandle& operator=(TaskedSendReceiverHandle&& other);
    /// Destructor
    ~TaskedSendReceiverHandle();

    /// Process group submissions (should be used for async requests)
    inline bool process(bool oneQueueInvocation = true) { return sendReceive(false, oneQueueInvocation); }
    /// Process local submissions (should be used for sync requests)
    inline bool processSync(bool oneQueueInvocation = true) { return sendReceive(true, oneQueueInvocation); }
    /// Adds a message to the submission queue
    bool sendSync(OriginalMessage* msg);
    /// Stops the handle thread if deamon
    void stop();
    /// Returns the underlying TaskedSendReceiver
    TaskedSendReceiver* get() { return _sendReceiver; }
    /// Returns the underlying TaskedSendReceiverGroup
    TaskedSendReceiverGroup* getGroup() { return _group; }
    /// Checks whether a TaskedSendReceiver is present
    inline bool has() const { return _sendReceiver; }

    friend TaskedSendReceiverGroup;
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
