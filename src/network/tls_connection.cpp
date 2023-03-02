#include "network/tls_connection.hpp"
#include "network/https_message.hpp"
#include "network/tls_context.hpp"
#include <iostream>
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
TLSConnection::TLSConnection(HTTPSMessage& message, TLSContext& context) : message(message), context(context), ssl(nullptr), state()
// The consturctor
{
    buffer = make_unique<char[]>(message.chunkSize);
}
//---------------------------------------------------------------------------
TLSConnection::~TLSConnection()
// The desturctor
{
    destroy();
}
//---------------------------------------------------------------------------
bool TLSConnection::init()
// Initialze SSL
{
    if (!context.ctx) {
        message.originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::TLS);
        return false;
    }
    ssl = SSL_new(context.ctx);
    if (!ssl) {
        message.originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::TLS);
        return false;
    }
    SSL_set_connect_state(ssl);
    BIO_new_bio_pair(&internalBio, message.chunkSize, &networkBio, message.chunkSize);
    SSL_set_bio(ssl, internalBio, internalBio);
    return true;
}
//---------------------------------------------------------------------------
void TLSConnection::destroy()
// Initialze SSL
{
    state.reset();
    if (ssl) {
        SSL_free(ssl);
        BIO_free(networkBio);
        ssl = nullptr;
    }
}
//---------------------------------------------------------------------------
template <typename F>
TLSConnection::Progress TLSConnection::operationHelper(IOUringSocket& socket, F&& func, int64_t& result)
// Helper function that handles the SSL_op calls
{
    if (state.progress == Progress::Finished || state.progress == Progress::Init) {
        auto status = func();
        auto error = SSL_get_error(ssl, status);
        switch (error) {
            case SSL_ERROR_NONE: {
                result = status;
                return Progress::Finished;
            }
            case SSL_ERROR_WANT_WRITE: // fallthrough
            case SSL_ERROR_WANT_READ: {
                state.progress = Progress::SendingInit;
                if (process(socket) != Progress::Aborted)
                    return Progress::Progress;
                return Progress::Aborted;
            }
            default:
                result = status;
                return Progress::Aborted;
        }
    } else if (state.progress == Progress::Aborted) {
        return Progress::Aborted;
    } else {
        process(socket);
        if (state.progress != Progress::Finished && state.progress != Progress::Aborted)
            return Progress::Progress;
        else
            return operationHelper(socket, func, result);
    }
}
//---------------------------------------------------------------------------
TLSConnection::Progress TLSConnection::recv(IOUringSocket& socket, char* buffer, int64_t bufferLength, int64_t& resultLength)
// Recv a TLS encrypted message
{
    auto ssl = this->ssl;
    auto sslRead = [ssl, buffer, bufferLength]() {
        return SSL_read(ssl, buffer, bufferLength);
    };
    return operationHelper(socket, sslRead, resultLength);
}
//---------------------------------------------------------------------------
TLSConnection::Progress TLSConnection::send(IOUringSocket& socket, const char* buffer, int64_t bufferLength, int64_t& resultLength)
// Send a TLS encrypted message
{
    auto ssl = this->ssl;
    auto sslWrite = [ssl, buffer, bufferLength]() {
        return SSL_write(ssl, buffer, bufferLength);
    };
    return operationHelper(socket, sslWrite, resultLength);
}
//---------------------------------------------------------------------------
TLSConnection::Progress TLSConnection::connect(IOUringSocket& socket)
// SSL/TLS connect
{
    int64_t unused;
    auto ssl = this->ssl;
    auto sslConnect = [ssl]() {
        return SSL_connect(ssl);
    };
    return operationHelper(socket, sslConnect, unused);
}
//---------------------------------------------------------------------------
TLSConnection::Progress TLSConnection::process(IOUringSocket& socket)
// Workhorse for sending and receiving the ssl encrypted messages using the shadow stack
{
    switch (state.progress) {
        case Progress::SendingInit: {
            // Check for send requirements from SSL
            state.reset();
            state.internalBioWrite = BIO_ctrl_pending(networkBio);
            if (state.internalBioWrite) {
                int64_t readSize = static_cast<int64_t>(message.chunkSize) > state.internalBioWrite ? state.internalBioWrite : message.chunkSize;
                state.networkBioRead = BIO_read(networkBio, buffer.get(), readSize);
            }
        } // fallthrough
        case Progress::Sending: {
            // Check for send requirements to the socket
            if (state.internalBioWrite) {
                // Not the first send part, so check request result
                if (state.progress == Progress::Sending) {
                    if (message.request->length > 0) {
                        state.socketWrite += message.request->length;
                    } else if (message.request->length != -EINPROGRESS && message.request->length != -EAGAIN) {
                        if (message.request->length == -ECANCELED || message.request->length == -EINTR)
                            message.originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Timeout);
                        else
                            message.originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Send);
                        state.progress = Progress::Aborted;
                        return state.progress;
                    }
                }
                if (state.networkBioRead != state.socketWrite) {
                    // As long as not finished
                    state.progress = Progress::Sending;
                    auto writeSize = state.networkBioRead - state.socketWrite;
                    const uint8_t* ptr = reinterpret_cast<uint8_t*>(buffer.get()) + state.socketWrite;
                    message.request = unique_ptr<IOUringSocket::Request>(new IOUringSocket::Request{{.cdata = ptr}, writeSize, message.fd, IOUringSocket::EventType::write, &message});
                    socket.send_prep_to(message.request.get(), &message.tcpSettings.kernelTimeout);
                    return state.progress;
                } else {
                    state.progress = Progress::ReceivingInit;
                }
            } else if (state.internalBioWrite) {
                state.progress = Progress::SendingInit;
                state.networkBioRead = 0;
                state.socketWrite = 0;
            } else {
                state.progress = Progress::ReceivingInit;
            }
        } // fallthrough
        case Progress::ReceivingInit: {
            // Check for recv requirements from SSL
            state.internalBioRead = BIO_ctrl_get_read_request(networkBio);
        } // fallthrough
        case Progress::Receiving: {
            // Check for recv requirements for the socket
            if (state.internalBioRead) {
                // Not the first send part, so check request result
                if (state.progress == Progress::Receiving) {
                    if (message.request->length == 0) {
                        message.originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Empty);
                        state.progress = Progress::Aborted;
                        return state.progress;
                    } else if (message.request->length > 0) {
                        state.networkBioWrite += BIO_write(networkBio, buffer.get() + state.socketRead, message.request->length);
                        state.socketRead += message.request->length;
                        assert(state.networkBioWrite == state.socketRead);
                    } else if (message.request->length != -EINPROGRESS && message.request->length != -EAGAIN) {
                        if (message.request->length == -ECANCELED || message.request->length == -EINTR)
                            message.originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Timeout);
                        else
                            message.originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Recv);
                        state.progress = Progress::Aborted;
                        return state.progress;
                    }
                }
                if (!state.networkBioWrite || state.networkBioWrite != state.socketRead) {
                    state.progress = Progress::Receiving;
                    int64_t readSize = static_cast<int64_t>(message.chunkSize) > (state.internalBioRead - state.socketRead) ? state.internalBioRead - state.socketRead : message.chunkSize;
                    uint8_t* ptr = reinterpret_cast<uint8_t*>(buffer.get()) + state.socketRead;
                    message.request = unique_ptr<IOUringSocket::Request>(new IOUringSocket::Request{{.data = ptr}, readSize, message.fd, IOUringSocket::EventType::read, &message});
                    socket.recv_prep_to(message.request.get(), &message.tcpSettings.kernelTimeout, message.tcpSettings.recvNoWait ? MSG_DONTWAIT : 0);
                    return state.progress;
                } else {
                    state.progress = Progress::Finished;
                    return state.progress;
                }
            } else if (state.internalBioRead) {
                state.progress = Progress::ReceivingInit;
                state.networkBioWrite = 0;
                state.socketRead = 0;
                return state.progress;
            } else {
                state.progress = Progress::Finished;
                return state.progress;
            }
        }
        default: {
            return state.progress;
        }
    }
}
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
