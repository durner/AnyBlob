#pragma once
#include "cloud/provider.hpp"
#include "network/http_helper.hpp"
#include "network/io_uring_socket.hpp"
#include "network/connection_manager.hpp"
#include "network/original_message.hpp"
#include "utils/data_vector.hpp"
#include <memory>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
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
class TLSContext;
class ConnectionManager;
//---------------------------------------------------------------------------
/// This implements a message task
/// After each execute invocation a new request was added to the uring queue and requires submission
struct MessageTask {
    /// Type of the message task
    enum class Type : uint8_t {
        HTTP,
        HTTPS
    };

    /// Original sending message
    OriginalMessage* originalMessage;
    /// The TCP Settings
    ConnectionManager::TCPSettings& tcpSettings;
    /// Send message
    std::unique_ptr<IOUringSocket::Request> request;
    /// The reduced offset in the send buffers
    int64_t sendBufferOffset;
    /// The reduced offset in the receive buffers
    int64_t receiveBufferOffset;
    /// The chunksize
    uint32_t chunkSize;
    /// The failures
    uint16_t failures;
    /// The message task class
    Type type;

    /// The failure limit
    static constexpr uint16_t failuresMax = 32;

    /// The pure virtual  callback
    virtual MessageState execute(ConnectionManager& connectionManager) = 0;
    /// The pure virtual destuctor
    virtual ~MessageTask(){};

    /// Builds the message task according to the sending message
    template <typename... Args>
    static std::unique_ptr<MessageTask> buildMessageTask(OriginalMessage* sendingMessage, Args&&... args) {
        std::string_view s(reinterpret_cast<char*>(sendingMessage->message->data()), sendingMessage->message->size());
        if (s.find("HTTP") != std::string_view::npos && sendingMessage->provider.getPort() == 443) {
            return std::make_unique<HTTPSMessage>(sendingMessage, std::forward<Args>(args)...);
        } else if (s.find("HTTP") != std::string_view::npos) {
            return std::make_unique<HTTPMessage>(sendingMessage, std::forward<Args>(args)...);
        }
        return nullptr;
    }

    protected:
    /// The constructor
    MessageTask(OriginalMessage* sendingMessage, ConnectionManager::TCPSettings& tcpSettings, uint32_t chunkSize);
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
