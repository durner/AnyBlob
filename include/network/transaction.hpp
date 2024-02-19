#pragma once
#include "cloud/provider.hpp"
#include "network/message_result.hpp"
#include "network/original_message.hpp"
#include <atomic>
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
    /// The message typedef
    using message_vector_type = std::vector<std::unique_ptr<network::OriginalMessage>>;

    /// The multipart upload struct
    struct MultipartUpload {
        /// The multipart state
        enum State : uint8_t {
            Default = 0,
            Sending = 1,
            Validating = 2,
            Aborted = 1u << 7
        };
        /// The uploadId
        std::string uploadId;
        /// The submessages
        message_vector_type messages;
        /// The eTags
        std::vector<std::string> eTags;
        /// The number of outstanding part requests
        std::atomic<int> outstanding;
        /// The state
        std::atomic<State> state;
        /// The error message id
        std::atomic<uint64_t> errorMessageId;

        /// The constructor
        explicit MultipartUpload(uint16_t parts) : messages(parts + 1), eTags(parts), outstanding(static_cast<int>(parts)), state(State::Default) {}
        /// Copy constructor
        MultipartUpload(MultipartUpload& other) = delete;
        /// Move constructor
        MultipartUpload(MultipartUpload&& other) noexcept : uploadId(std::move(other.uploadId)), messages(std::move(other.messages)), eTags(std::move(other.eTags)), outstanding(other.outstanding.load()), state(other.state.load()) {}
        /// Copy assignment
        MultipartUpload& operator=(MultipartUpload other) = delete;
    };

    /// The provider
    cloud::Provider* _provider;

    /// The message
    message_vector_type _messages;
    // The message coutner
    std::atomic<uint64_t> _messageCounter;
    /// Multipart uploads
    std::vector<MultipartUpload> _multipartUploads;
    /// Finished multipart uploads
    std::atomic<uint64_t> _completedMultiparts;

    /// Helper function to build callback messages
    template <typename Callback, typename... Arguments>
    std::unique_ptr<OriginalCallbackMessage<Callback>> makeCallbackMessage(Callback&& c, Arguments&&... args) {
        return std::make_unique<network::OriginalCallbackMessage<Callback>>(std::forward<Callback>(c), std::forward<Arguments>(args)...);
    }

    public:
    /// The constructor
    Transaction() : _provider(), _messages(), _messageCounter(), _multipartUploads(), _completedMultiparts() {}
    /// The explicit constructor with the provider
    explicit Transaction(cloud::Provider* provider) : _provider(provider), _messages(), _messageCounter(), _multipartUploads(), _completedMultiparts() {}

    /// Set the provider
    constexpr void setProvider(cloud::Provider* provider) { this->_provider = provider; }
    /// Sends the request messages to the task group
    void processAsync(network::TaskedSendReceiverGroup& group);
    /// Processes the request messages
    void processSync(TaskedSendReceiver& sendReceiver);

    /// Function to ensure fresh keys before creating messages
    /// This is needed to ensure valid keys before a message is requested
    /// Simply forward a task send receiver the message function and the args of this message
    template <typename Function>
    bool verifyKeyRequest(TaskedSendReceiver& sendReceiver, Function&& func) {
        assert(_provider);
        _provider->initSecret(sendReceiver);
        return std::forward<Function>(func)();
    }

    /// Build a new get request for synchronous calls
    /// Note that the range is [start, end[, [0, 0[ gets the whole object
    inline bool getObjectRequest(const std::string& remotePath, std::pair<uint64_t, uint64_t> range = {0, 0}, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        assert(_provider);
        _provider->getSecret();
        auto originalMsg = std::make_unique<network::OriginalMessage>(_provider->getRequest(remotePath, range), _provider->getAddress(), _provider->getPort(), result, capacity, traceId);
        if (!originalMsg)
            return false;
        _messages.push_back(std::move(originalMsg));
        return true;
    }

    /// Build a new get request with callback
    /// Note that the range is [start, end[, [0, 0[ gets the whole object
    template <typename Callback>
    inline bool getObjectRequest(Callback&& callback, const std::string& remotePath, std::pair<uint64_t, uint64_t> range = {0, 0}, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        assert(_provider);
        _provider->getSecret();
        auto originalMsg = std::make_unique<network::OriginalCallbackMessage<Callback>>(std::forward<Callback>(callback), _provider->getRequest(remotePath, range), _provider->getAddress(), _provider->getPort(), result, capacity, traceId);
        if (!originalMsg)
            return false;
        _messages.push_back(std::move(originalMsg));
        return true;
    }

    /// Build a new put request for synchronous calls
    inline bool putObjectRequest(const std::string& remotePath, const char* data, uint64_t size, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        assert(_provider);
        _provider->getSecret();
        if (_provider->multipartUploadSize() && size > _provider->multipartUploadSize())
            return putObjectRequestMultiPart(remotePath, data, size, result, capacity, traceId);
        auto object = std::string_view(data, size);
        auto originalMsg = std::make_unique<network::OriginalMessage>(_provider->putRequest(remotePath, object), _provider->getAddress(), _provider->getPort(), result, capacity, traceId);
        originalMsg->setPutRequestData(reinterpret_cast<const uint8_t*>(data), size);
        if (!originalMsg)
            return false;
        _messages.push_back(std::move(originalMsg));
        return true;
    }

    /// Build a new put request with callback
    template <typename Callback>
    inline bool putObjectRequest(Callback&& callback, const std::string& remotePath, const char* data, uint64_t size, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        assert(_provider);
        _provider->getSecret();
        if (_provider->multipartUploadSize() && size > _provider->multipartUploadSize())
            return putObjectRequestMultiPart(std::forward<Callback>(callback), remotePath, data, size, result, capacity, traceId);
        auto object = std::string_view(data, size);
        auto originalMsg = std::make_unique<network::OriginalCallbackMessage<Callback>>(std::forward<Callback>(callback), _provider->putRequest(remotePath, object), _provider->getAddress(), _provider->getPort(), result, capacity, traceId);
        originalMsg->setPutRequestData(reinterpret_cast<const uint8_t*>(data), size);
        if (!originalMsg)
            return false;
        _messages.push_back(std::move(originalMsg));
        return true;
    }

    /// Build a new delete request for synchronous calls
    inline bool deleteObjectRequest(const std::string& remotePath, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        assert(_provider);
        _provider->getSecret();
        auto originalMsg = std::make_unique<network::OriginalMessage>(_provider->deleteRequest(remotePath), _provider->getAddress(), _provider->getPort(), result, capacity, traceId);
        if (!originalMsg)
            return false;
        _messages.push_back(std::move(originalMsg));
        return true;
    }

    /// Build a new delete request with callback
    template <typename Callback>
    inline bool deleteObjectRequest(Callback&& callback, const std::string& remotePath, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        assert(_provider);
        _provider->getSecret();
        auto originalMsg = std::make_unique<network::OriginalCallbackMessage<Callback>>(std::forward<Callback>(callback), _provider->deleteRequest(remotePath), _provider->getAddress(), _provider->getPort(), result, capacity, traceId);
        if (!originalMsg)
            return false;
        _messages.push_back(std::move(originalMsg));
        return true;
    }

    private:
    /// Build a new put request for synchronous calls
    inline bool putObjectRequestMultiPart(const std::string& remotePath, const char* data, uint64_t size, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        auto finished = [](network::MessageResult& /*result*/) {};
        return putObjectRequestMultiPart(std::move(finished), remotePath, data, size, result, capacity, traceId);
    }

    /// Build a new put request with callback
    template <typename Callback>
    inline bool putObjectRequestMultiPart(Callback&& callback, const std::string& remotePath, const char* data, uint64_t size, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        assert(_provider);
        auto splitSize = _provider->multipartUploadSize();
        uint16_t parts = static_cast<uint16_t>((size / splitSize) + ((size % splitSize) ? 1u : 0u));
        _multipartUploads.emplace_back(parts);
        auto position = _multipartUploads.size() - 1;

        auto uploadMessages = [callback = std::forward<Callback>(callback), position, parts, data, remotePath, traceId, splitSize, size, this](network::MessageResult& initalRequestResult) {
            if (!initalRequestResult.success()) {
                _completedMultiparts++;
                return;
            }
            _provider->getSecret();
            _multipartUploads[position].uploadId = _provider->getUploadId(initalRequestResult.getResult());
            auto offset = 0ull;
            for (uint16_t i = 1; i <= parts; i++) {
                auto finishMultipart = [&callback, &initalRequestResult, position, remotePath, traceId, i, parts, this](network::MessageResult& result) {
                    _provider->getSecret();
                    if (!result.success()) [[unlikely]] {
                        _multipartUploads[position].errorMessageId = i - 1;
                        _multipartUploads[position].state = MultipartUpload::State::Aborted;
                    } else {
                        _multipartUploads[position].eTags[i - 1] = _provider->getETag(std::string_view(reinterpret_cast<const char*>(result.getData()), result.getOffset()));
                    }
                    if (_multipartUploads[position].outstanding.fetch_sub(1) == 1) {
                        if (_multipartUploads[position].state != MultipartUpload::State::Aborted) [[likely]] {
                            auto contentData = std::make_unique<std::string>();
                            auto content = contentData.get();
                            auto finished = [&callback, &initalRequestResult, contentPtr = move(contentData), this](network::MessageResult& result) mutable {
                                if (!result.success()) {
                                    initalRequestResult.state = network::MessageState::Cancelled;
                                    initalRequestResult.originError = &result;
                                }
                                _completedMultiparts++;
                                callback(initalRequestResult);
                            };
                            auto originalMsg = makeCallbackMessage(std::move(finished), _provider->completeMultiPartRequest(remotePath, _multipartUploads[position].uploadId, _multipartUploads[position].eTags, *content), _provider->getAddress(), _provider->getPort(), nullptr, 0, traceId);
                            originalMsg->setPutRequestData(reinterpret_cast<const uint8_t*>(content->data()), content->size());
                            _multipartUploads[position].messages[parts] = std::move(originalMsg);
                        } else {
                            auto finished = [&callback, &initalRequestResult, position, this](network::MessageResult& /*result*/) {
                                initalRequestResult.state = network::MessageState::Cancelled;
                                initalRequestResult.originError = &_multipartUploads[position].messages[_multipartUploads[position].errorMessageId]->result;
                                _completedMultiparts++;
                                callback(initalRequestResult);
                            };
                            auto originalMsg = makeCallbackMessage(std::move(finished), _provider->deleteRequestGeneric(remotePath, _multipartUploads[position].uploadId), _provider->getAddress(), _provider->getPort(), nullptr, 0, traceId);
                            _multipartUploads[position].messages[parts] = std::move(originalMsg);
                        }
                        _multipartUploads[position].state = MultipartUpload::State::Validating;
                    }
                };
                auto partSize = (i != parts) ? splitSize : size - offset;
                auto object = std::string_view(data + offset, partSize);
                auto originalMsg = makeCallbackMessage(std::move(finishMultipart), _provider->putRequestGeneric(remotePath, object, i, _multipartUploads[position].uploadId), _provider->getAddress(), _provider->getPort(), nullptr, 0, traceId);
                originalMsg->setPutRequestData(reinterpret_cast<const uint8_t*>(data + offset), partSize);
                _multipartUploads[position].messages[i - 1] = std::move(originalMsg);
                offset += partSize;
            }
            _multipartUploads[position].state = MultipartUpload::State::Sending;
        };

        auto originalMsg = makeCallbackMessage(std::move(uploadMessages), _provider->createMultiPartRequest(remotePath), _provider->getAddress(), _provider->getPort(), result, capacity, traceId);
        if (!originalMsg)
            return false;
        _messages.push_back(std::move(originalMsg));
        return true;
    }

    public:
    /// The iterator
    using iterator = Iterator;
    /// The const iterator
    using const_iterator = ConstIterator;

    /// The begin it
    inline iterator begin() { return iterator(_messages.begin()); }
    /// The end it
    inline iterator end() { return iterator(_messages.end()); }

    /// The begin it
    inline const_iterator cbegin() const { return const_iterator(_messages.cbegin()); }
    /// The end it
    inline const_iterator cend() const { return const_iterator(_messages.cend()); }

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
        explicit constexpr Iterator(const message_vector_type::iterator& it) { this->it = it; };
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
        explicit constexpr ConstIterator(const message_vector_type::const_iterator& it) { this->it = it; }
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
