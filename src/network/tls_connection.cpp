#include "network/tls_connection.hpp"
#include "cloud/provider.hpp"
#include "network/connection_manager.hpp"
#include "network/https_message.hpp"
#include "network/socket.hpp"
#include "network/tls_context.hpp"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <memory>
#include <utility>
#include <arpa/inet.h>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <sys/socket.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2023
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
TLSConnection::TLSConnection(TLSContext& context) : _message(nullptr), _context(context), _ssl(nullptr), _buffer(), _bufferOffset(0), _bufferLength(0), _sent(0), _record(), _pending(false), _hostname(), _port(0), _verifyPeer(false)
// The constructor
{
}
//---------------------------------------------------------------------------
TLSConnection::~TLSConnection()
// The destructor
{
    destroy();
}
//---------------------------------------------------------------------------
BIO* TLSConnection::createBio()
// Create the bio that moves the records over the socket
{
    static BIO_METHOD* method = []() {
        auto* created = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "anyblob uring");
        if (created) {
            // OpenSSL asks in char and int, and reads a retry flag instead of a short count
            BIO_meth_set_read(created, [](BIO* bio, char* out, int length) {
                BIO_clear_retry_flags(bio);
                auto read = static_cast<TLSConnection*>(BIO_get_data(bio))->bioRead({reinterpret_cast<uint8_t*>(out), static_cast<size_t>(length)});
                if (!read)
                    BIO_set_retry_read(bio);
                return read ? static_cast<int>(read) : -1;
            });
            BIO_meth_set_write(created, [](BIO* bio, const char* data, int length) {
                BIO_clear_retry_flags(bio);
                auto written = static_cast<TLSConnection*>(BIO_get_data(bio))->bioWrite({reinterpret_cast<const uint8_t*>(data), static_cast<size_t>(length)});
                if (!written)
                    BIO_set_retry_write(bio);
                return written ? static_cast<int>(written) : -1;
            });
            // Every record leaves the bio right away, so only the flush of OpenSSL has to succeed
            BIO_meth_set_ctrl(created, [](BIO*, int command, long, void*) -> long { return command == BIO_CTRL_FLUSH ? 1 : 0; });
        }
        return created;
    }();
    return method ? BIO_new(method) : nullptr;
}
//---------------------------------------------------------------------------
uint64_t TLSConnection::bioRead(span<uint8_t> out)
// Read a ciphertext from the staging buffer
{
    auto available = min(static_cast<uint64_t>(out.size()), _bufferLength - _bufferOffset);
    memcpy(out.data(), _buffer.data() + _bufferOffset, available);
    _bufferOffset += available;
    return available;
}
//---------------------------------------------------------------------------
uint64_t TLSConnection::bioWrite(span<const uint8_t> record)
// Remember the record OpenSSL wants sent
{
    // OpenSSL calls again to finish the send
    if (_sent)
        return exchange(_sent, 0);
    _record = record;
    return 0;
}
//---------------------------------------------------------------------------
void TLSConnection::queueReceive(Socket& socket)
// Queue the next ciphertext receive
{
    _bufferOffset = 0;
    _bufferLength = 0;
    assert(in_range<int64_t>(_buffer.size()));
    _message->request = make_unique<Socket::Request>(Socket::Request{.data = {.data = _buffer.data()}, .length = static_cast<int64_t>(_buffer.size()), .fd = _message->fd, .event = Socket::EventType::read, .messageTask = _message});
    socket.prepare(*_message->request, _message->attemptTimeout(), _message->tcpSettings.recvNoWait ? MSG_DONTWAIT : 0);
    _pending = true;
}
//---------------------------------------------------------------------------
void TLSConnection::queueSend(Socket& socket)
// Queue the record for sending
{
    assert(in_range<int64_t>(_record.size()));
    _message->request = make_unique<Socket::Request>(Socket::Request{.data = {.cdata = _record.data()}, .length = static_cast<int64_t>(_record.size()), .fd = _message->fd, .event = Socket::EventType::write, .messageTask = _message});
    socket.prepare(*_message->request, _message->attemptTimeout());
    _pending = true;
}
//---------------------------------------------------------------------------
void TLSConnection::fail(MessageFailureCode failure)
// Record a message failure
{
    _message->originalMessage->result.failureCode |= static_cast<uint16_t>(failure);
}
//---------------------------------------------------------------------------
bool TLSConnection::consumeCompletion(MessageFailureCode failure)
// Consume the completed events
{
    _pending = false;
    auto& request = *_message->request;
    if (request.length < 0) {
        if (request.length == -EAGAIN || request.length == -EINPROGRESS)
            return true;
        if (request.length == -ECANCELED || request.length == -EINTR || request.length == -ETIMEDOUT)
            fail(MessageFailureCode::Timeout);
        else
            fail(failure);
        return false;
    }
    if (!request.length) {
        fail(MessageFailureCode::Empty);
        return false;
    }
    if (request.event == Socket::EventType::read)
        _bufferLength = static_cast<uint64_t>(request.length);
    else
        _sent = static_cast<uint64_t>(request.length);
    return true;
}
//---------------------------------------------------------------------------
bool TLSConnection::init(HTTPSMessage* message)
// Initialize SSL
{
    _message = message;
    if (!_ssl) {
        if (!_context._ctx) {
            fail(MessageFailureCode::TLS);
            return false;
        }
        _ssl = SSL_new(_context._ctx);
        auto bio = _ssl ? createBio() : nullptr;
        if (!bio) {
            fail(MessageFailureCode::TLS);
            return false;
        }
        SSL_set_ex_data(_ssl, TLSContext::connectionSlot(), this);
        SSL_set_connect_state(_ssl);
        BIO_set_data(bio, this);
        BIO_set_init(bio, 1);
        // SSL owns the shared read/write BIO after this call
        SSL_set_bio(_ssl, bio, bio);
        auto& provider = _message->originalMessage->provider;
        _hostname = provider.getAddress();
        _port = provider.getPort();
        _verifyPeer = provider.verifyPeer();
        sockaddr_in6 numeric;
        auto named = inet_pton(AF_INET, _hostname.c_str(), &numeric) != 1 && inet_pton(AF_INET6, _hostname.c_str(), &numeric) != 1;
        if (named && !SSL_ctrl(_ssl, SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name, const_cast<char*>(_hostname.c_str()))) {
            fail(MessageFailureCode::TLS);
            return false;
        }
        if (_verifyPeer) {
            if (!_context.hasTrustStore()) {
                fail(MessageFailureCode::TLS);
                fail(MessageFailureCode::Certificate);
                return false;
            }
            SSL_set_verify(_ssl, SSL_VERIFY_PEER, nullptr);
            SSL_set_hostflags(_ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
            auto checked = named ? SSL_set1_host(_ssl, _hostname.c_str()) : X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(_ssl), _hostname.c_str());
            if (checked != 1) {
                fail(MessageFailureCode::TLS);
                return false;
            }
        }
        _context.reuseSession(_hostname, _port, _verifyPeer, _ssl);
    }
    // Resize the buffer to the chunk size
    if (_buffer.size() < message->chunkSize)
        _buffer.resize(message->chunkSize);
    _pending = false;
    _sent = 0;
    return true;
}
//---------------------------------------------------------------------------
void TLSConnection::destroy()
// Destroy SSL state
{
    SSL_free(exchange(_ssl, nullptr));
    _message = nullptr;
    _pending = false;
    _bufferOffset = 0;
    _bufferLength = 0;
    _sent = 0;
}
//---------------------------------------------------------------------------
template <typename F>
TLSConnection::Progress TLSConnection::runOperation(ConnectionManager& connectionManager, F operation, int64_t& result, MessageFailureCode failure)
// Run an SSL operation and queue the transfer it needs
{
    if (_pending && !consumeCompletion(failure))
        return Progress::Aborted;

    ERR_clear_error();
    auto status = operation();
    auto error = SSL_get_error(_ssl, status);
    switch (error) {
        case SSL_ERROR_NONE: {
            result = status;
            return Progress::Finished;
        }
        case SSL_ERROR_WANT_READ: {
            queueReceive(connectionManager.getSocketConnection());
            return Progress::Pending;
        }
        case SSL_ERROR_WANT_WRITE: {
            queueSend(connectionManager.getSocketConnection());
            return Progress::Pending;
        }
        default: {
            result = status;
            // A peer that closed without answering is the empty result of the plain socket path
            if (error == SSL_ERROR_ZERO_RETURN || !status)
                fail(MessageFailureCode::Empty);
            else
                fail(MessageFailureCode::TLS);
            ERR_clear_error();
            return Progress::Aborted;
        }
    }
}
//---------------------------------------------------------------------------
TLSConnection::Progress TLSConnection::recv(ConnectionManager& connectionManager, char* buffer, int64_t bufferLength, int64_t& resultLength)
// Recv a TLS encrypted message
{
    assert(in_range<int>(bufferLength));
    auto sslRead = [this, buffer, bufferLength = static_cast<int>(bufferLength)]() {
        return SSL_read(_ssl, buffer, bufferLength);
    };
    return runOperation(connectionManager, sslRead, resultLength, MessageFailureCode::Recv);
}
//---------------------------------------------------------------------------
TLSConnection::Progress TLSConnection::send(ConnectionManager& connectionManager, const char* buffer, int64_t bufferLength, int64_t& resultLength)
// Send a TLS encrypted message
{
    assert(in_range<int>(bufferLength));
    auto sslWrite = [this, buffer, bufferLength = static_cast<int>(bufferLength)]() {
        return SSL_write(_ssl, buffer, bufferLength);
    };
    return runOperation(connectionManager, sslWrite, resultLength, MessageFailureCode::Send);
}
//---------------------------------------------------------------------------
TLSConnection::Progress TLSConnection::connect(ConnectionManager& connectionManager)
// SSL/TLS connect
{
    if (SSL_is_init_finished(_ssl))
        return Progress::Finished;
    int64_t unused = 0;
    auto sslConnect = [this]() {
        return SSL_connect(_ssl);
    };
    auto status = runOperation(connectionManager, sslConnect, unused, MessageFailureCode::TLS);
    if (status == Progress::Finished || status == Progress::Aborted)
        return verifyCertificate(status);
    return status;
}
//---------------------------------------------------------------------------
TLSConnection::Progress TLSConnection::verifyCertificate(Progress status)
// Abort on a rejected peer certificate
{
    if (!_verifyPeer || SSL_get_verify_result(_ssl) == X509_V_OK)
        return status;
    fail(MessageFailureCode::Certificate);
    _context.dropSession(_hostname, _port);
    return Progress::Aborted;
}
//---------------------------------------------------------------------------
TLSConnection::Progress TLSConnection::shutdown(ConnectionManager& connectionManager)
// SSL/TLS shutdown
{
    int64_t unused = 0;
    auto sslShutdown = [this]() {
        auto status = SSL_shutdown(_ssl);
        // The close notify was sent, the answer of the peer is not awaited
        return !status ? 1 : status;
    };
    auto status = runOperation(connectionManager, sslShutdown, unused, MessageFailureCode::TLS);
    if (status == Progress::Aborted)
        _context.dropSession(_hostname, _port);
    return status;
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
