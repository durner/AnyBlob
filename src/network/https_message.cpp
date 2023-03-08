#include "network/https_message.hpp"
#include "network/message_result.hpp"
#include "utils/data_vector.hpp"
#include "utils/timer.hpp"
#include <memory>
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
HTTPSMessage::HTTPSMessage(OriginalMessage* message, TLSContext& context, uint64_t chunkSize) : HTTPMessage(message, chunkSize)
// The constructor
{
    type = Type::HTTPS;
    tlsLayer = make_unique<TLSConnection>(*this, context);
}
//---------------------------------------------------------------------------
MessageState HTTPSMessage::execute(IOUringSocket& socket)
// executes the task
{
    auto& state = originalMessage->result.state;
    switch (state) {
        case MessageState::Init: {
            try {
                fd = socket.connect(originalMessage->hostname, originalMessage->port, tcpSettings);
            } catch (exception& /*e*/) {
                originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Socket);
                state = MessageState::Aborted;
                return state;
            }
            if (!tlsLayer->init()) {
                originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::TLS);
                reset(socket, failures++ > failuresMax);
                return execute(socket);
            }
            state = MessageState::TLSHandshake;
            sendBufferOffset = 0;
        } // fallthrough
        case MessageState::TLSHandshake: {
            // Handshake procedure
            auto status = tlsLayer->connect(socket);
            if (status == TLSConnection::Progress::Finished) {
                state = MessageState::InitSending;
            } else if (status == TLSConnection::Progress::Aborted) {
                originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::TLS);
                reset(socket, failures++ > failuresMax);
                return execute(socket);
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
                length = originalMessage->putLength + originalMessage->message->size() - sendBufferOffset;
            }

            int64_t result = 0;
            auto status = tlsLayer->send(socket, reinterpret_cast<const char*>(ptr), length, result);
            if (status == TLSConnection::Progress::Finished) {
                sendBufferOffset += result;
                if (sendBufferOffset >= static_cast<int64_t>(originalMessage->message->size() + originalMessage->putLength)) {
                    state = MessageState::InitReceiving;
                    receiveBufferOffset = 0;
                    originalMessage->result.getDataVector().clear();
                    originalMessage->result.getDataVector().resize(originalMessage->result.getDataVector().size() + chunkSize);
                }
                return execute(socket);
            } else if (status == TLSConnection::Progress::Aborted) {
                reset(socket, failures++ > failuresMax);
                return execute(socket);
            }
            return state;
        }
        case MessageState::InitReceiving: // fallhrough
        case MessageState::Receiving: {
            auto& receive = originalMessage->result.getDataVector();
            int64_t result = 0;
            auto status = tlsLayer->recv(socket, reinterpret_cast<char*>(receive.data() + receiveBufferOffset), chunkSize, result);
            state = MessageState::Receiving;
            if (status == TLSConnection::Progress::Finished) {
                receive.resize(receive.size() - (chunkSize - result));
                receiveBufferOffset += result;
                try {
                    if (HTTPHelper::finished(receive.data(), receiveBufferOffset, info)) {
                        originalMessage->result.size = info->length;
                        originalMessage->result.offset = info->headerLength;
                        state = MessageState::TLSShutdown;
                        return execute(socket);
                    } else {
                        // resize and grow capacity
                        if (receive.capacity() < receive.size() + chunkSize && info) {
                            receive.reserve(max(info->length + info->headerLength + chunkSize, static_cast<uint64_t>(receive.capacity() * 1.5)));
                        }
                        receive.resize(receive.size() + chunkSize);
                        return execute(socket);
                    }
                } catch (exception&) {
                    originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::HTTP);
                    reset(socket, failures++ > failuresMax);
                    return execute(socket);
                }
            } else if (status == TLSConnection::Progress::Aborted) {
                reset(socket, failures++ > failuresMax);
                return execute(socket);
            }
            return state;
        }
        case MessageState::TLSShutdown: {
            // Shutdown procedure
            auto status = tlsLayer->shutdown(socket);
            // The request was successful even if the shutdown fails
            if (status == TLSConnection::Progress::Finished || status == TLSConnection::Progress::Aborted) {
                socket.disconnect(request->fd, originalMessage->hostname, originalMessage->port, &tcpSettings, sendBufferOffset + receiveBufferOffset);
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
void HTTPSMessage::reset(IOUringSocket& socket, bool aborted)
// Reset for restart
{
    tlsLayer = make_unique<TLSConnection>(*this, tlsLayer->getContext());
    HTTPMessage::reset(socket, aborted);
}
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
