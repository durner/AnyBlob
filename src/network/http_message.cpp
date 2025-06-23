#include "network/http_message.hpp"
#include "network/connection_manager.hpp"
#include "network/http_response.hpp"
#include "network/message_result.hpp"
#include "utils/data_vector.hpp"
#include <memory>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
HTTPMessage::HTTPMessage(OriginalMessage* message, ConnectionManager::TCPSettings& tcpSettings, uint32_t chunkSize) : MessageTask(message, tcpSettings, chunkSize), info()
// The constructor
{
    type = Type::HTTP;
}
//---------------------------------------------------------------------------
MessageState HTTPMessage::execute(ConnectionManager& connectionManager)
// executes the task
{
    int32_t fd = -1;
    auto& state = originalMessage->result.state;
    switch (state) {
        case MessageState::Init: {
            try {
                fd = connectionManager.connect(originalMessage->provider.getAddress(), originalMessage->provider.getPort(), false, tcpSettings);
            } catch (exception& /*e*/) {
                if (request)
                    request->fd = -1;
                originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Socket);
                reset(connectionManager, failures++ > connectionFailuresMax);
                return execute(connectionManager);
            }
            state = MessageState::InitSending;
            sendBufferOffset = 0;
        } // fallthrough
        case MessageState::InitSending: // fallthrough
        case MessageState::Sending: {
            if (state != MessageState::InitSending) {
                fd = request->fd;
                if (request->length > 0) {
                    sendBufferOffset += request->length;
                } else if (request->length != -EINPROGRESS && request->length != -EAGAIN) {
                    if (request->length == -ECANCELED || request->length == -EINTR) {
                        originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Timeout);
                        reset(connectionManager, failures++ > failuresMax);
                        return execute(connectionManager);
                    } else {
                        originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Send);
                        state = MessageState::InitReceiving;
                        receiveBufferOffset = 0;
                        originalMessage->result.getDataVector().clear();
                        return execute(connectionManager);
                    }
                }

                if (sendBufferOffset >= static_cast<int64_t>(originalMessage->message->size() + originalMessage->putLength)) {
                    state = MessageState::InitReceiving;
                    receiveBufferOffset = 0;
                    originalMessage->result.getDataVector().clear();
                    return execute(connectionManager);
                }
            }
            state = MessageState::Sending;
            {
                const uint8_t* ptr;
                ptr = originalMessage->message->data() + sendBufferOffset;
                auto length = static_cast<int64_t>(originalMessage->message->size()) - sendBufferOffset;
                if (originalMessage->putLength > 0 && sendBufferOffset >= static_cast<int64_t>(originalMessage->message->size())) {
                    ptr = originalMessage->putData + sendBufferOffset - originalMessage->message->size();
                    length = static_cast<int64_t>(originalMessage->putLength + originalMessage->message->size()) - sendBufferOffset;
                }
                request = std::make_unique<Socket::Request>(Socket::Request{.data = {.cdata = ptr}, .length = length, .fd = fd, .event = Socket::EventType::write, .messageTask = this});
                if (length <= static_cast<int64_t>(chunkSize))
                    connectionManager.getSocketConnection().send_to(*request, tcpSettings.timeout);
                else
                    connectionManager.getSocketConnection().send(*request);
            }
            break;
        }
        case MessageState::InitReceiving: // fallhrough
        case MessageState::Receiving: {
            auto& receive = originalMessage->result.getDataVector();
            // check after first successful receiving if we are finished
            if (state != MessageState::InitReceiving) {
                if (request->length == 0) {
                    originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Empty);
                    reset(connectionManager, failures++ > failuresMax);
                    return execute(connectionManager);
                } else if (request->length > 0) {
                    // resize according to real downloaded size
                    receive.resize(receive.size() - (chunkSize - static_cast<uint64_t>(request->length)));
                    receiveBufferOffset += request->length;

                    try {
                        // check whether finished http
                        if (HttpHelper::finished(receive.data(), static_cast<uint64_t>(receiveBufferOffset), info)) {
                            originalMessage->result.response = move(info);
                            auto success = HttpResponse::checkSuccess(originalMessage->result.response->response.code);
                            connectionManager.disconnect(request->fd, &tcpSettings, static_cast<uint64_t>(sendBufferOffset + receiveBufferOffset), !success);
                            if (success) {
                                state = MessageState::Finished;
                            } else {
                                originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::HTTP);
                                state = MessageState::Aborted;
                            }
                            return state;
                        }
                    } catch (exception&) {
                        originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::HTTP);
                        reset(connectionManager, failures++ > failuresMax);
                        return execute(connectionManager);
                    }
                } else if (request->length != -EINPROGRESS && request->length != -EAGAIN) {
                    if (request->length == -ECANCELED || request->length == -EINTR)
                        originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Timeout);
                    else
                        originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Recv);
                    reset(connectionManager, failures++ > failuresMax);
                    return execute(connectionManager);
                } else {
                    receive.resize(receive.size() - chunkSize);
                }
                // resize and grow capacity
                if (receive.capacity() < receive.size() + chunkSize && info) {
                    receive.reserve(max(info->length + info->headerLength + chunkSize, receive.capacity() + receive.capacity() / 2));
                }
            }
            receive.resize(receive.size() + chunkSize);
            request = std::make_unique<Socket::Request>(Socket::Request{.data = {.data = receive.data() + receiveBufferOffset}, .length = static_cast<int64_t>(chunkSize), .fd = request->fd, .event = Socket::EventType::read, .messageTask = this});
            connectionManager.getSocketConnection().recv_to(*request, tcpSettings.timeout, tcpSettings.recvNoWait ? MSG_DONTWAIT : 0);
            state = MessageState::Receiving;
            break;
        }
        default:
            break;
    }
    return state;
}
//---------------------------------------------------------------------------
void HTTPMessage::reset(ConnectionManager& connectionManager, bool aborted)
// Reset for restart
{
    if (!aborted) {
        originalMessage->result.getDataVector().clear();
        receiveBufferOffset = 0;
        sendBufferOffset = 0;
        info.reset();
        originalMessage->result.state = MessageState::Init;
    } else {
        originalMessage->result.state = MessageState::Aborted;
    }
    if ((originalMessage->result.failureCode & static_cast<uint16_t>(MessageFailureCode::HTTP)) && originalMessage->provider.supportsResigning()) {
        originalMessage->message = originalMessage->provider.resignRequest(*originalMessage->message, originalMessage->putData, originalMessage->putLength);
    }
    if (request && request->fd >= 0) {
        connectionManager.disconnect(request->fd, &tcpSettings, 0, true);
        request->fd = -1;
    }
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
