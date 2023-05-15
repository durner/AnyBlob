#include "network/messages.hpp"
#include "utils/data_vector.hpp"
#include "utils/timer.hpp"
#include <memory>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
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
MessageTask::MessageTask(OriginalMessage* message, uint8_t* receiveBuffer, uint64_t bufferSize) : originalMessage(message), sendBufferOffset(0), receiveBufferOffset(0), failures(0)
// The constructor
{
    if (receiveBuffer)
        originalMessage->result = make_unique<utils::DataVector<uint8_t>>(receiveBuffer, bufferSize);
}
//---------------------------------------------------------------------------
HTTPMessage::HTTPMessage(OriginalMessage* message, uint64_t chunkSize, uint8_t* receiveBuffer, uint64_t bufferSize) : MessageTask(message, receiveBuffer, bufferSize), chunkSize(chunkSize), info()
// The constructor
{
    type = Type::HTTP;
}
//---------------------------------------------------------------------------
MessageState HTTPMessage::execute(IOUringSocket& socket)
// executes the task
{
    int32_t fd = -1;
    auto& state = originalMessage->state;
    switch (state) {
        case MessageState::Init: {
            try {
                fd = socket.connect(originalMessage->hostname, originalMessage->port, tcpSettings);
            } catch (exception& /*e*/) {
                state = MessageState::Aborted;
                return execute(socket);
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
                } else {
                    reset(socket, failures++ > failuresMax);
                    return execute(socket);
                }

                if (sendBufferOffset >= static_cast<int64_t>(originalMessage->message->size() + originalMessage->putLength)) {
                    state = MessageState::InitReceiving;
                    if (!originalMessage->result)
                        originalMessage->result = make_unique<utils::DataVector<uint8_t>>();
                    receiveBufferOffset = 0;
                    originalMessage->result->clear();
                    return execute(socket);
                }
            }
            state = MessageState::Sending;
            {
                const uint8_t* ptr;
                ptr = originalMessage->message->data() + sendBufferOffset;
                auto length = static_cast<int64_t>(originalMessage->message->size()) - sendBufferOffset;
                if (originalMessage->putLength > 0 && sendBufferOffset >= static_cast<int64_t>(originalMessage->message->size())) {
                    ptr = originalMessage->putData + sendBufferOffset - originalMessage->message->size();
                    length = originalMessage->putLength + originalMessage->message->size() - sendBufferOffset;
                }
                request = unique_ptr<IOUringSocket::Request>(new IOUringSocket::Request{{.cdata = ptr}, length, fd, IOUringSocket::EventType::write, this});
                socket.send_prep(request.get());
            }
            break;
        }
        case MessageState::InitReceiving: // fallhrough
        case MessageState::Receiving: {
            auto& receive = originalMessage->result;
            // check after first successful receiving if we are finished
            if (state != MessageState::InitReceiving) {
                if (request->length >= 0) {
                    // resize according to real downloaded size
                    receive->resize(receive->size() - (chunkSize - request->length));
                    receiveBufferOffset += request->length;

                    try {
                        // check whether finished http
                        if (HTTPHelper::finished(receive->data(), receiveBufferOffset, info)) {
                            state = MessageState::Finished;
                            socket.disconnect(request->fd, originalMessage->hostname, originalMessage->port, &tcpSettings, sendBufferOffset + receiveBufferOffset);
                            return originalMessage->state;
                        }
                    } catch (exception&) {
                        string header(reinterpret_cast<char*>(receive->data()), receive->size());
                        reset(socket, failures++ > failuresMax);
                        return execute(socket);
                    }

                    if (!request->length) {
                        string header(reinterpret_cast<char*>(receive->data()), receive->size());
                        cerr << "Empty request - restart!" << endl;
                        reset(socket, failures++ > failuresMax);
                        return execute(socket);
                    }
                } else if (errno != EINPROGRESS) {
                    if (socket.checkTimeout(request->fd, tcpSettings)) {
                        string header(reinterpret_cast<char*>(receive->data()), receive->size());
                        cerr << "Timeout error! Error code: " << strerror(errno) << endl;
                        reset(socket, failures++ > failuresMax);
                        return execute(socket);
                    }
                    string header(reinterpret_cast<char*>(receive->data()), receive->size());
                    cerr << "Request error! Error code: " << strerror(errno) << endl;
                    reset(socket, failures++ > failuresMax);
                    return execute(socket);
                } else {
                    receive->resize(receive->size() - chunkSize);
                }
                // resize and grow capacity
                if (receive->capacity() < receive->size() + chunkSize && info) {
                    receive->reserve(max(info->length + info->headerLength + chunkSize, static_cast<uint64_t>(receive->capacity() * 1.5)));
                }
            }
            receive->resize(receive->size() + chunkSize);
            request = unique_ptr<IOUringSocket::Request>(new IOUringSocket::Request{{.data = receive->data() + receiveBufferOffset}, static_cast<int64_t>(chunkSize), request->fd, IOUringSocket::EventType::read, this});
            socket.recv_prep(request.get(), tcpSettings.recvNoWait ? MSG_DONTWAIT : 0);
            state = MessageState::Receiving;
            break;
        }
        case MessageState::Aborted:
            break;
        default:
            break;
    }
    return state;
}
//---------------------------------------------------------------------------
void HTTPMessage::reset(IOUringSocket& socket, bool aborted)
// Reset for restart
{
    if (!aborted) {
        originalMessage->result->clear();
        receiveBufferOffset = 0;
        sendBufferOffset = 0;
        originalMessage->state = MessageState::Init;
    } else {
        originalMessage->state = MessageState::Aborted;
    }
    socket.disconnect(request->fd, originalMessage->hostname, originalMessage->port, &tcpSettings, true);
}
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
