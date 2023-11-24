#include "network/tls_connection.hpp"
#include "network/https_message.hpp"
#include "network/tls_context.hpp"
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <cassert>
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
TLSConnection::TLSConnection(HTTPSMessage& message, TLSContext& context) : _message(message), _context(context), _ssl(nullptr), _state()
// The consturctor
{
    _buffer = make_unique<char[]>(message.chunkSize);
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
    if (!_context._ctx) {
        _message.originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::TLS);
        return false;
    }
    _ssl = SSL_new(_context._ctx);
    if (!_ssl) {
        _message.originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::TLS);
        return false;
    }
    SSL_set_connect_state(_ssl);
    BIO_new_bio_pair(&_internalBio, _message.chunkSize, &_networkBio, _message.chunkSize);
    SSL_set_bio(_ssl, _internalBio, _internalBio);
    _context.reuseSession(_message.fd, _ssl);
    return true;
}
//---------------------------------------------------------------------------
void TLSConnection::destroy()
// Initialze SSL
{
    _state.reset();
    if (_ssl) {
        SSL_free(_ssl);
        BIO_free(_networkBio);
        _ssl = nullptr;
    }
}
//---------------------------------------------------------------------------
template <typename F>
TLSConnection::Progress TLSConnection::operationHelper(IOUringSocket& socket, F&& func, int64_t& result)
// Helper function that handles the SSL_op calls
{
    if (_state.progress == Progress::Finished || _state.progress == Progress::Init) {
        auto status = func();
        auto error = SSL_get_error(_ssl, status);
        switch (error) {
            case SSL_ERROR_NONE: {
                result = status;
                return Progress::Finished;
            }
            case SSL_ERROR_WANT_WRITE: // fallthrough
            case SSL_ERROR_WANT_READ: {
                _state.progress = Progress::SendingInit;
                if (process(socket) != Progress::Aborted)
                    return Progress::Progress;
                return Progress::Aborted;
            }
            default:
                result = status;
                return Progress::Aborted;
        }
    } else if (_state.progress == Progress::Aborted) {
        return Progress::Aborted;
    } else {
        process(socket);
        if (_state.progress != Progress::Finished && _state.progress != Progress::Aborted)
            return Progress::Progress;
        else
            return operationHelper(socket, func, result);
    }
}
//---------------------------------------------------------------------------
TLSConnection::Progress TLSConnection::recv(IOUringSocket& socket, char* buffer, int64_t bufferLength, int64_t& resultLength)
// Recv a TLS encrypted message
{
    auto ssl = this->_ssl;
    auto sslRead = [ssl, buffer, bufferLength]() {
        return SSL_read(ssl, buffer, bufferLength);
    };
    return operationHelper(socket, sslRead, resultLength);
}
//---------------------------------------------------------------------------
TLSConnection::Progress TLSConnection::send(IOUringSocket& socket, const char* buffer, int64_t bufferLength, int64_t& resultLength)
// Send a TLS encrypted message
{
    auto ssl = this->_ssl;
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
    auto ssl = this->_ssl;
    auto sslConnect = [ssl]() {
        return SSL_connect(ssl);
    };
    return operationHelper(socket, sslConnect, unused);
}
//---------------------------------------------------------------------------
TLSConnection::Progress TLSConnection::shutdown(IOUringSocket& socket, bool failedOnce)
// SSL/TLS shutdown
{
    int64_t unused;
    auto ssl = this->_ssl;
    auto sslShutdown = [ssl]() {
        return SSL_shutdown(ssl);
    };
    auto status = operationHelper(socket, sslShutdown, unused);
    if (status == Progress::Finished) {
        _context.cacheSession(_message.fd, ssl);
    } else if (status == Progress::Aborted) {
        if (!failedOnce) [[likely]] {
            status = Progress::Init;
            return shutdown(socket, true);
        } else {
            _context.dropSession(_message.fd);
        }
    }
    return status;
}
//---------------------------------------------------------------------------
TLSConnection::Progress TLSConnection::process(IOUringSocket& socket)
// Workhorse for sending and receiving the ssl encrypted messages using the shadow stack
{
    switch (_state.progress) {
        case Progress::SendingInit: {
            // Check for send requirements from SSL
            _state.reset();
            _state.internalBioWrite = BIO_ctrl_pending(_networkBio);
            if (_state.internalBioWrite) {
                int64_t readSize = static_cast<int64_t>(_message.chunkSize) > _state.internalBioWrite ? _state.internalBioWrite : _message.chunkSize;
                _state.networkBioRead = BIO_read(_networkBio, _buffer.get(), readSize);
            }
        } // fallthrough
        case Progress::Sending: {
            // Check for send requirements to the socket
            if (_state.internalBioWrite) {
                // Not the first send part, so check request result
                if (_state.progress == Progress::Sending) {
                    if (_message.request->length > 0) {
                        _state.socketWrite += _message.request->length;
                    } else if (_message.request->length != -EINPROGRESS && _message.request->length != -EAGAIN) {
                        if (_message.request->length == -ECANCELED || _message.request->length == -EINTR)
                            _message.originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Timeout);
                        else
                            _message.originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Send);
                        _state.progress = Progress::Aborted;
                        return _state.progress;
                    }
                }
                if (_state.networkBioRead != _state.socketWrite) {
                    // As long as not finished
                    _state.progress = Progress::Sending;
                    auto writeSize = _state.networkBioRead - _state.socketWrite;
                    const uint8_t* ptr = reinterpret_cast<uint8_t*>(_buffer.get()) + _state.socketWrite;
                    _message.request = unique_ptr<IOUringSocket::Request>(new IOUringSocket::Request{{.cdata = ptr}, writeSize, _message.fd, IOUringSocket::EventType::write, &_message});
                    if (writeSize <= static_cast<int64_t>(_message.chunkSize))
                        socket.send_prep_to(_message.request.get(), &_message.tcpSettings.kernelTimeout);
                    else
                        socket.send_prep(_message.request.get());
                    return _state.progress;
                } else {
                    _state.progress = Progress::ReceivingInit;
                }
            } else if (_state.internalBioWrite) {
                _state.progress = Progress::SendingInit;
                _state.networkBioRead = 0;
                _state.socketWrite = 0;
            } else {
                _state.progress = Progress::ReceivingInit;
            }
        } // fallthrough
        case Progress::ReceivingInit: {
            // Check for recv requirements from SSL
            _state.internalBioRead = BIO_ctrl_get_read_request(_networkBio);
        } // fallthrough
        case Progress::Receiving: {
            // Check for recv requirements for the socket
            if (_state.internalBioRead) {
                // Not the first send part, so check request result
                if (_state.progress == Progress::Receiving) {
                    if (_message.request->length == 0) {
                        _message.originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Empty);
                        _state.progress = Progress::Aborted;
                        return _state.progress;
                    } else if (_message.request->length > 0) {
                        _state.networkBioWrite += BIO_write(_networkBio, _buffer.get() + _state.socketRead, _message.request->length);
                        _state.socketRead += _message.request->length;
                        assert(_state.networkBioWrite == _state.socketRead);
                    } else if (_message.request->length != -EINPROGRESS && _message.request->length != -EAGAIN) {
                        if (_message.request->length == -ECANCELED || _message.request->length == -EINTR)
                            _message.originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Timeout);
                        else
                            _message.originalMessage->result.failureCode |= static_cast<uint16_t>(MessageFailureCode::Recv);
                        _state.progress = Progress::Aborted;
                        return _state.progress;
                    }
                }
                if (!_state.networkBioWrite || _state.networkBioWrite != _state.socketRead) {
                    _state.progress = Progress::Receiving;
                    int64_t readSize = static_cast<int64_t>(_message.chunkSize) > (_state.internalBioRead - _state.socketRead) ? _state.internalBioRead - _state.socketRead : _message.chunkSize;
                    uint8_t* ptr = reinterpret_cast<uint8_t*>(_buffer.get()) + _state.socketRead;
                    _message.request = unique_ptr<IOUringSocket::Request>(new IOUringSocket::Request{{.data = ptr}, readSize, _message.fd, IOUringSocket::EventType::read, &_message});
                    socket.recv_prep_to(_message.request.get(), &_message.tcpSettings.kernelTimeout, _message.tcpSettings.recvNoWait ? MSG_DONTWAIT : 0);
                    return _state.progress;
                } else {
                    _state.progress = Progress::Finished;
                    return _state.progress;
                }
            } else if (_state.internalBioRead) {
                _state.progress = Progress::ReceivingInit;
                _state.networkBioWrite = 0;
                _state.socketRead = 0;
                return _state.progress;
            } else {
                _state.progress = Progress::Finished;
                return _state.progress;
            }
        }
        default: {
            return _state.progress;
        }
    }
}
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
