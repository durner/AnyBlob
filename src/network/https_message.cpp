#include "network/https_message.hpp"
#include "network/connection_manager.hpp"
#include "network/http_response.hpp"
#include "network/message_result.hpp"
#include "utils/data_vector.hpp"
#include "utils/timer.hpp"
#include <cassert>
#include <memory>
#include <utility>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2023
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace network {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
HTTPSMessage::HTTPSMessage(OriginalMessage* message, ConnectionManager::TCPSettings& tcpSettings, uint32_t chunkSize) : HTTPMessage(message, tcpSettings, chunkSize)
// The constructor
{
    type = Type::HTTPS;
}
//---------------------------------------------------------------------------
MessageState HTTPSMessage::execute(ConnectionManager& connectionManager)
// executes the task
{
    auto& state = originalMessage->result.state;
    switch (state) {
        case MessageState::Init: {
            try {
                fd = connectionManager.connect(originalMessage->provider.getAddress(), originalMessage->provider.getPort(), true, tcpSettings);
            } catch (exception& /*e*/) {
                originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Socket);
                reset(connectionManager, failures++ > failuresMax);
                return execute(connectionManager);
            }
            tlsLayer = connectionManager.getTLSConnection(fd);
            if (!tlsLayer->init(this)) {
                originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::TLS);
                reset(connectionManager, failures++ > failuresMax);
                return execute(connectionManager);
            }
            state = MessageState::TLSHandshake;
            sendBufferOffset = 0;
        } // fallthrough
        case MessageState::TLSHandshake: {
            // Handshake procedure
            auto status = tlsLayer->connect(connectionManager);
            if (status == TLSConnection::Progress::Finished) {
                state = MessageState::InitSending;
            } else if (status == TLSConnection::Progress::Aborted) {
                originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::TLS);
                reset(connectionManager, failures++ > failuresMax);
                return execute(connectionManager);
            } else {
                return state;
            }
        } // fallthrough
        case MessageState::InitSending: // fallthrough
        case MessageState::Sending: {
            const uint8_t* ptr;
            ptr = originalMessage->message->data() + sendBufferOffset;
            auto length = static_cast<int64_t>(originalMessage->message->size()) - sendBufferOffset;
            if (originalMessage->putLength > 0 && sendBufferOffset >= static_cast<int64_t>(originalMessage->message->size())) {
                ptr = originalMessage->putData + sendBufferOffset - originalMessage->message->size();
                length = static_cast<int64_t>(originalMessage->putLength + originalMessage->message->size()) - sendBufferOffset;
            }

            int64_t result = 0;
            auto status = tlsLayer->send(connectionManager, reinterpret_cast<const char*>(ptr), length, result);
            if (status == TLSConnection::Progress::Finished) {
                sendBufferOffset += result;
                if (sendBufferOffset >= static_cast<int64_t>(originalMessage->message->size() + originalMessage->putLength)) {
                    state = MessageState::InitReceiving;
                    receiveBufferOffset = 0;
                    originalMessage->result.getDataVector().clear();
                    originalMessage->result.getDataVector().resize(originalMessage->result.getDataVector().size() + chunkSize);
                }
                return execute(connectionManager);
            } else if (status == TLSConnection::Progress::Aborted) {
                reset(connectionManager, failures++ > failuresMax);
                return execute(connectionManager);
            }
            return state;
        }
        case MessageState::InitReceiving: // fallhrough
        case MessageState::Receiving: {
            auto& receive = originalMessage->result.getDataVector();
            int64_t result = 0;
            assert(in_range<int64_t>(chunkSize));
            auto status = tlsLayer->recv(connectionManager, reinterpret_cast<char*>(receive.data() + receiveBufferOffset), static_cast<int64_t>(chunkSize), result);
            state = MessageState::Receiving;
            if (status == TLSConnection::Progress::Finished) {
                receive.resize(receive.size() - (chunkSize - static_cast<uint64_t>(result)));
                receiveBufferOffset += result;
                try {
                    if (HttpHelper::finished(receive.data(), static_cast<uint64_t>(receiveBufferOffset), info)) {
                        originalMessage->result.response = move(info);
                        if (HttpResponse::checkSuccess(originalMessage->result.response->response.code)) {
                            state = MessageState::TLSShutdown;
                        } else {
                            originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::HTTP);
                            reset(connectionManager, true);
                        }
                        // Decide if to cache the session
                        if (tcpSettings.reuse) {
                            state = MessageState::Finished;
                            connectionManager.disconnect(request->fd, originalMessage->provider.getAddress(), originalMessage->provider.getPort(), &tcpSettings, static_cast<uint64_t>(sendBufferOffset + receiveBufferOffset));
                            return state;
                        } else {
                            state = MessageState::TLSShutdown;
                        }
                        return execute(connectionManager);
                    } else {
                        // resize and grow capacity
                        if (receive.capacity() < receive.size() + chunkSize && info) {
                            receive.reserve(max(info->length + info->headerLength + chunkSize, receive.capacity() + receive.capacity() / 2));
                        }
                        receive.resize(receive.size() + chunkSize);
                        return execute(connectionManager);
                    }
                } catch (exception&) {
                    originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::HTTP);
                    reset(connectionManager, failures++ > failuresMax);
                    return execute(connectionManager);
                }
            } else if (status == TLSConnection::Progress::Aborted) {
                reset(connectionManager, failures++ > failuresMax);
                return execute(connectionManager);
            }
            return state;
        }
        case MessageState::TLSShutdown: {
            // Shutdown procedure
            auto status = tlsLayer->shutdown(connectionManager);
            // The request was successful even if the shutdown fails
            if (status == TLSConnection::Progress::Finished || status == TLSConnection::Progress::Aborted) {
                connectionManager.disconnect(request->fd, originalMessage->provider.getAddress(), originalMessage->provider.getPort(), &tcpSettings, static_cast<uint64_t>(sendBufferOffset + receiveBufferOffset));
                state = MessageState::Finished;
                return MessageState::Finished;
            } else {
                return state;
            }
        } // fallthrough
        case MessageState::Finished:
            break;
        case MessageState::Aborted:
            break;
        default:
            break;
    }
    return state;
}
//---------------------------------------------------------------------------
void HTTPSMessage::reset(ConnectionManager& connectionManager, bool aborted)
// Reset for restart
{
    HTTPMessage::reset(connectionManager, aborted);
}
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
