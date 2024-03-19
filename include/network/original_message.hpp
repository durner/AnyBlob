#pragma once
#include "network/message_result.hpp"
#include "utils/data_vector.hpp"
#include <atomic>
#include <memory>
#include <string>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
//---------------------------------------------------------------------------
namespace utils {
class Timer;
}; // namespace utils
//---------------------------------------------------------------------------
namespace cloud {
class Provider;
}; // namespace cloud
//---------------------------------------------------------------------------
namespace network {
//---------------------------------------------------------------------------
/// This is the original request message struct
struct OriginalMessage {
    /// The message
    std::unique_ptr<utils::DataVector<uint8_t>> message;
    /// The provider that knows the address, the port and, optionally signed the messsage
    cloud::Provider& provider;
    /// The result
    MessageResult result;

    /// If it is a put request store the additional data
    /// The raw data ptr for put requests
    const uint8_t* putData;
    /// The length
    uint64_t putLength;

    /// Optional trace info
    uint64_t traceId;

    /// The constructor
    OriginalMessage(std::unique_ptr<utils::DataVector<uint8_t>> message, cloud::Provider& provider, uint8_t* receiveBuffer = nullptr, uint64_t bufferSize = 0, uint64_t traceId = 0) : message(std::move(message)), provider(provider), result(receiveBuffer, bufferSize), putData(nullptr), putLength(), traceId(traceId) {}

    /// The destructor
    virtual ~OriginalMessage() = default;

    /// Add the put request data to the message
    constexpr void setPutRequestData(const uint8_t* data, uint64_t length) {
        this->putData = data;
        this->putLength = length;
    }

    /// Set the result vector and transfer ownership
    inline void setResultVector(utils::DataVector<uint8_t>* dataVector) {
        result.dataVector = std::unique_ptr<utils::DataVector<uint8_t>>(dataVector);
    }

    /// Callback required
    virtual inline bool requiresFinish() { return false; }

    /// Callback
    virtual inline void finish() {}
};
//---------------------------------------------------------------------------
/// The callback original message
template <typename Callback>
struct OriginalCallbackMessage : public OriginalMessage {
    /// The callback
    Callback callback;

    /// The constructor
    OriginalCallbackMessage(Callback&& callback, std::unique_ptr<utils::DataVector<uint8_t>> message, cloud::Provider& provider, uint8_t* receiveBuffer = nullptr, uint64_t bufferSize = 0, uint64_t traceId = 0) : OriginalMessage(std::move(message), provider, receiveBuffer, bufferSize, traceId), callback(std::forward<Callback>(callback)) {}

    /// The destructor
    virtual ~OriginalCallbackMessage() override = default;

    /// Override if callback required
    bool inline requiresFinish() override { return true; }

    /// Callback
    void inline finish() override {
        callback(result);
    }
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
