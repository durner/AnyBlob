#pragma once
#include "cloud/provider.hpp"
#include "network/message_result.hpp"
#include "network/original_message.hpp"
#include <cassert>
#include <memory>
#include <span>
#include <string>
#include <vector>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2023
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace cloud {
class Provider;
} // namespace cloud
//---------------------------------------------------------------------------
namespace network {
//---------------------------------------------------------------------------
struct OriginalMessage;
class TaskedSendReceiver;
class TaskedSendReceiverGroup;
//---------------------------------------------------------------------------
/// The request/response transaction handler used to transfer data from and to remote storage
class Transaction {
    public:
    class Iterator;
    class ConstIterator;

    protected:
    /// The provider
    const cloud::Provider* provider;
    /// The message typedef
    using message_vector_type = std::vector<std::unique_ptr<network::OriginalMessage>>;
    /// The message
    message_vector_type messages;

    public:

    /// The constructor
    Transaction() = default;
    /// The explicit constructor with the provider
    explicit Transaction(const cloud::Provider* provider) : provider(provider), messages() {}

    /// Set the provider
    constexpr void setProvider(const cloud::Provider* provider) { this->provider = provider; }
    /// Sends the request messages to the task group
    void processAsync(TaskedSendReceiver& sendReceiver);
    /// Processes the request messages
    void processSync(TaskedSendReceiver& sendReceiver);

    /// Build a new get request for synchronous calls
    /// Note that the range is [start, end[, [0, 0[ gets the whole object
    inline void getObjectRequest(const std::string& remotePath, std::pair<uint64_t, uint64_t> range = {0, 0}, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        assert(provider);
        auto originalMsg = std::make_unique<network::OriginalMessage>(provider->getRequest(remotePath, range), provider->getAddress(), provider->getPort(), result, capacity, traceId);
        messages.push_back(std::move(originalMsg));
    }

    /// Build a new get request with callback
    /// Note that the range is [start, end[, [0, 0[ gets the whole object
    template <typename Callback>
    inline void getObjectRequest(Callback&& callback, const std::string& remotePath, std::pair<uint64_t, uint64_t> range = {0, 0}, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        assert(provider);
        auto originalMsg = std::make_unique<network::OriginalCallbackMessage<Callback>>(std::forward<Callback&&>(callback), provider->getRequest(remotePath, range), provider->getAddress(), provider->getPort(), result, capacity, traceId);
        messages.push_back(std::move(originalMsg));
    }

    /// Build a new put request for synchronous calls
    inline void putObjectRequest(const std::string& remotePath, const char* data, uint64_t size, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        assert(provider);
        auto object = std::string_view(data, size);
        auto originalMsg = std::make_unique<network::OriginalMessage>(provider->putRequest(remotePath, object), provider->getAddress(), provider->getPort(), result, capacity, traceId);
        originalMsg->setPutRequestData(reinterpret_cast<const uint8_t*>(data), size);
        messages.push_back(std::move(originalMsg));
    }

    /// Build a new put request with callback
    template <typename Callback>
    inline void putObjectRequest(Callback&& callback, const std::string& remotePath, const char* data, uint64_t size, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        assert(provider);
        auto object = std::string_view(data, size);
        auto originalMsg = std::make_unique<network::OriginalCallbackMessage<Callback>>(std::forward<Callback&&>(callback), provider->putRequest(remotePath, object), provider->getAddress(), provider->getPort(), result, capacity, traceId);
        originalMsg->setPutRequestData(reinterpret_cast<const uint8_t*>(data), size);
        messages.push_back(std::move(originalMsg));
    }

    /// Build a new delete request for synchronous calls
    inline void deleteObjectRequest(const std::string& remotePath, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        assert(provider);
        auto originalMsg = std::make_unique<network::OriginalMessage>(provider->deleteRequest(remotePath), provider->getAddress(), provider->getPort(), result, capacity, traceId);
        messages.push_back(std::move(originalMsg));
    }

    /// Build a new delete request with callback
    template <typename Callback>
    inline void deleteObjectRequest(Callback&& callback, const std::string& remotePath, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        assert(provider);
        auto originalMsg = std::make_unique<network::OriginalCallbackMessage<Callback>>(std::forward<Callback&&>(callback), provider->deleteRequest(remotePath), provider->getAddress(), provider->getPort(), result, capacity, traceId);
        messages.push_back(std::move(originalMsg));
    }

    /// The iterator
    using iterator = Iterator;
    /// The const iterator
    using const_iterator = ConstIterator;

    /// The begin it
    inline iterator begin() { return iterator(messages.begin()); }
    /// The end it
    inline iterator end() { return iterator(messages.end()); }

    /// The begin it
    inline const_iterator cbegin() const { return const_iterator(messages.cbegin()); }
    /// The end it
    inline const_iterator cend() const { return const_iterator(messages.cend()); }

    /// The iterator
    class Iterator {
        /// The value type
        using value_type = MessageResult;
        /// The diff type
        using difference_type = std::ptrdiff_t;
        /// The reference type
        using reference = value_type&;
        /// The pointer type
        using pointer = value_type*;
        /// The iterator category
        using iterator_category = std::forward_iterator_tag;

        private:
        /// The iterator
        message_vector_type::iterator it;

        /// Constructor with iterator
        constexpr Iterator(const message_vector_type::iterator& it) { this->it = it; };
        /// Copy constructor
        constexpr Iterator(const Iterator& it) { this->it = it.it; }
        /// Delete default constructor
        Iterator() = delete;

        public:
        /// Reference
        reference operator*() const;
        /// Pointer
        pointer operator->() const;

        /// Inc
        inline Iterator& operator++() {
            ++it;
            return *this;
        }
        /// Post-inc
        inline Iterator operator++(int) {
            Iterator prv(*this);
            operator++();
            return prv;
        }

        /// Equality
        constexpr bool operator==(const Iterator& other) const { return it == other.it; }

        friend Transaction;
    };

    /// The const iterator
    class ConstIterator {
        /// The const value type
        using value_type = const MessageResult;
        /// The diff type
        using difference_type = std::ptrdiff_t;
        /// The reference type
        using reference = value_type&;
        /// The pointer type
        using pointer = value_type*;
        /// The iterator category
        using iterator_category = std::forward_iterator_tag;

        private:
        /// The iterator
        message_vector_type::const_iterator it;

        /// Constructor with iterator
        constexpr ConstIterator(const message_vector_type::const_iterator& it) { this->it = it; }
        /// Copy constructor
        constexpr ConstIterator(const ConstIterator& it) { this->it = it.it; }
        /// Delete default constructor
        ConstIterator() = delete;

        public:
        /// Reference
        reference operator*() const;
        /// Pointer
        pointer operator->() const;

        /// Inc
        inline ConstIterator& operator++() {
            ++it;
            return *this;
        }
        /// Post-inc
        inline ConstIterator operator++(int) {
            ConstIterator prv(*this);
            operator++();
            return prv;
        }

        /// Equality
        constexpr bool operator==(const ConstIterator& other) const { return it == other.it; }

        friend Transaction;
    };
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
