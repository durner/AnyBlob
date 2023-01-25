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
namespace network {
//---------------------------------------------------------------------------
/// This is the original request message struct
struct OriginalMessage {
    /// The message
    std::unique_ptr<utils::DataVector<uint8_t>> message;
    /// The result
    MessageResult result;

    /// The hostname
    std::string hostname;
    /// The port
    uint32_t port;

    /// Optional trace info
    uint64_t traceId;

    /// If it is a put request store the additional data
    /// The raw data ptr for put requests
    const uint8_t* putData;
    /// The length
    uint64_t putLength;

    /// The constructor
    OriginalMessage(std::unique_ptr<utils::DataVector<uint8_t>> message, std::string hostname, uint32_t port, uint8_t* receiveBuffer = nullptr, uint64_t bufferSize = 0, uint64_t traceId = 0) : message(std::move(message)), result(receiveBuffer, bufferSize), hostname(hostname), port(port), traceId(traceId), putData(nullptr), putLength() {}

    /// The destructor
    virtual ~OriginalMessage() = default;

    /// Add the put request data to the message
    void setPutRequestData(const uint8_t* data, uint64_t length) {
        this->putData = data;
        this->putLength = length;
    }

    /// Set the result vector and transfer ownership
    void setResultVector(utils::DataVector<uint8_t>* dataVector) {
        result.dataVector = std::unique_ptr<utils::DataVector<uint8_t>>(dataVector);
    }

    /// Callback required
    virtual bool requiresFinish() { return false; }

    /// Callback
    virtual void finish() {}
};
//---------------------------------------------------------------------------
/// The callback original message
template <typename Callback>
struct OriginalCallbackMessage : public OriginalMessage {
    /// The callback
    Callback callback;

    /// The constructor
    OriginalCallbackMessage(Callback&& callback, std::unique_ptr<utils::DataVector<uint8_t>> message, std::string hostname, uint32_t port, uint8_t* receiveBuffer = nullptr, uint64_t bufferSize = 0, uint64_t traceId = 0) : OriginalMessage(std::move(message), hostname, port, receiveBuffer, bufferSize, traceId), callback(std::forward<Callback>(callback)) {}

    /// The destructor
    virtual ~OriginalCallbackMessage() override = default;

    /// Override if callback required
    bool requiresFinish() override { return true; }

    /// Callback
    void finish() override {
        callback(result);
    }
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
