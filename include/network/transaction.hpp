#pragma once
#include "network/message_result.hpp"
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
/// Used as interface in the initiation GetTransaction and PutTransaction
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

    /// The constructor
    Transaction() = default;
    /// The explicit constructor with the provider
    explicit Transaction(const cloud::Provider* provider) : provider(provider), messages() {}

    public:
    /// Set the provider
    constexpr void setProvider(const cloud::Provider* provider) { this->provider = provider; }
    /// Sends the request messages to the task group
    void processAsync(TaskedSendReceiver& sendReceiver);
    /// Processes the request messages
    void processSync(TaskedSendReceiver& sendReceiver);

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
