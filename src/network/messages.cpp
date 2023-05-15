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
MessageTask::MessageTask(OriginalMessage* message, uint8_t* receiveBuffer, uint64_t bufferSize) : originalMessage(message), sendBufferOffset(0), receiveBufferOffset(0), status(Status::Init), failures(0)
// The constructor
{
    if (receiveBuffer) {
        receive = make_unique<utils::DataVector<uint8_t>>(receiveBuffer, bufferSize);
    }
}
//---------------------------------------------------------------------------
HTTPMessage::HTTPMessage(OriginalMessage* message, uint64_t chunkSize, uint8_t* receiveBuffer, uint64_t bufferSize) : MessageTask(message, receiveBuffer, bufferSize), chunkSize(chunkSize), info()
// The constructor
{
    type = Type::HTTP;
}
//---------------------------------------------------------------------------
MessageTask::Status HTTPMessage::execute(IOUringSocket& socket)
// executes the task
{
    int32_t fd = -1;
    switch (status) {
        case Status::Init: {
            try {
                fd = socket.connect(originalMessage->hostname, originalMessage->port, tcpSettings);
            } catch (exception& e) {
                if (failures++ > failuresMax)
                    throw std::runtime_error(string("HTTPMessage execute reaches failure limit: ") + e.what());
                return execute(socket);
            }
            sendBufferOffset = 0;
            status = Status::InitSending;
        } // fallthrough
        case Status::InitSending: // fallthrough
        case Status::Sending: {
            if (status != Status::InitSending) {
                fd = request->fd;
                if (request->length > 0) {
                    sendBufferOffset += request->length;
                } else {
                    if (failures++ > failuresMax)
                        throw std::runtime_error("HTTPMessage execute reaches failure limit!");
                    reset(socket);
                    return execute(socket);
                }

                if (sendBufferOffset >= static_cast<int64_t>(originalMessage->message->size() + originalMessage->length)) {
                    status = Status::InitReceiving;
                    if (!receive) {
                        receive = make_unique<utils::DataVector<uint8_t>>();
                    }
                    receiveBufferOffset = 0;
                    receive->clear();
                    return execute(socket);
                }
            }
            status = Status::Sending;
            {
                auto ptr = originalMessage->message->data() + sendBufferOffset;
                auto length = static_cast<int64_t>(originalMessage->message->size()) - sendBufferOffset;
                if (originalMessage->length > 0 && sendBufferOffset >= static_cast<int64_t>(originalMessage->message->size())) {
                    ptr = originalMessage->data + sendBufferOffset - originalMessage->message->size();
                    length = originalMessage->length + originalMessage->message->size() - sendBufferOffset;
                }
                request = unique_ptr<IOUringSocket::Request>(new IOUringSocket::Request{ptr, length, fd, IOUringSocket::EventType::write, this});
                socket.send_prep(request.get());
            }
            break;
        }
        case Status::InitReceiving: {
            // fallhrough
        }
        case Status::Receiving: {
            // check after first successful receiving if we are finished
            if (status != Status::InitReceiving) {
                if (request->length >= 0) {
                    // resize according to real downloaded size
                    receive->resize(receive->size() - (chunkSize - request->length));
                    receiveBufferOffset += request->length;

                    try {
                        // check whether finished http
                        if (HTTPHelper::finished(receive->data(), receiveBufferOffset, info)) {
                            status = Status::Finished;
                            socket.disconnect(request->fd, originalMessage->hostname, originalMessage->port, &tcpSettings, sendBufferOffset + receiveBufferOffset);
                            return status;
                        }
                    } catch (exception&) {
                        string header(reinterpret_cast<char*>(receive->data()), receive->size());
                        if (failures++ > failuresMax)
                            throw std::runtime_error(string("HTTPMessage execute reaches failure limit!\n") + header);
                        reset(socket);
                        return execute(socket);
                    }

                    if (!request->length) {
                        string header(reinterpret_cast<char*>(receive->data()), receive->size());
                        cerr << "Empty request - restart!" << endl;
                        if (failures++ > failuresMax)
                            throw std::runtime_error(string("HTTPMessage execute reaches failure limit!\n") + header);
                        reset(socket);
                        return execute(socket);
                    }
                } else if (errno != EINPROGRESS) {
                    if (socket.checkTimeout(request->fd, tcpSettings)) {
                        string header(reinterpret_cast<char*>(receive->data()), receive->size());
                        cerr << "Timeout error! Error code: " << strerror(errno) << endl;
                        if (failures++ > failuresMax)
                            throw std::runtime_error(string("HTTPMessage execute reaches failure limit!\n") + header);
                        reset(socket);
                        return execute(socket);
                    }
                    string header(reinterpret_cast<char*>(receive->data()), receive->size());
                    cerr << "Request error! Error code: " << strerror(errno) << endl;
                    if (failures++ > failuresMax)
                        throw std::runtime_error(string("HTTPMessage execute reaches failure limit!\n") + header);
                    reset(socket);
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
            request = unique_ptr<IOUringSocket::Request>(new IOUringSocket::Request{receive->data() + receiveBufferOffset, static_cast<int64_t>(chunkSize), request->fd, IOUringSocket::EventType::read, this});
            socket.recv_prep(request.get(), tcpSettings.recvNoWait ? MSG_DONTWAIT : 0);
            status = Status::Receiving;
            break;
        }
        default:
            break;
    }
    return status;
}
//---------------------------------------------------------------------------
void HTTPMessage::reset(IOUringSocket& socket)
// Reset for restart
{
    receive->clear();
    receiveBufferOffset = 0;
    sendBufferOffset = 0;
    status = Status::Init;
    socket.disconnect(request->fd, originalMessage->hostname, originalMessage->port, &tcpSettings, true);
}
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
