#include "tls_proxy.hpp"
#include <memory>
#include <stdexcept>
#include <vector>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// CedarDB (Dominik Durner), 2026
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::test {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
TlsProxy::TlsProxy(const string& target) : LoopbackProxy(target, "tls")
// The constructor listens on a free loopback port and starts the proxy
{
    createContext();
    start([this](int client, uint64_t) { relay(client); });
}
//---------------------------------------------------------------------------
TlsProxy::~TlsProxy()
// The destructor stops the proxy and closes all sockets
{
    stop();
    SSL_CTX_free(_ctx);
}
//---------------------------------------------------------------------------
void TlsProxy::createContext()
// Build the context with a self signed certificate for the loopback
{
    unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(EVP_RSA_gen(2048), EVP_PKEY_free);
    unique_ptr<X509, decltype(&X509_free)> certificate(X509_new(), X509_free);
    auto created = key && certificate;
    if (created) {
        X509_set_version(certificate.get(), 2);
        ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1);
        X509_gmtime_adj(X509_getm_notBefore(certificate.get()), 0);
        X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 3600);
        X509_set_pubkey(certificate.get(), key.get());
        auto name = X509_get_subject_name(certificate.get());
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("127.0.0.1"), -1, -1, 0);
        X509_set_issuer_name(certificate.get(), name);
        created = X509_sign(certificate.get(), key.get(), EVP_sha256()) > 0;
    }
    if (created) {
        _ctx = SSL_CTX_new(TLS_server_method());
        created = _ctx && SSL_CTX_use_certificate(_ctx, certificate.get()) == 1 && SSL_CTX_use_PrivateKey(_ctx, key.get()) == 1;
    }
    if (!created) {
        if (_ctx)
            SSL_CTX_free(_ctx);
        throw runtime_error("tls proxy cannot create its certificate");
    }
}
//---------------------------------------------------------------------------
void TlsProxy::relay(int client)
// Terminate TLS for one connection and relay it to the target
{
    unique_ptr<SSL, decltype(&SSL_free)> ssl(SSL_new(_ctx), SSL_free);
    auto server = -1;
    // The accepted socket blocks, so the handshake either completes or fails
    if (ssl && SSL_set_fd(ssl.get(), client) && SSL_accept(ssl.get()) == 1) {
        server = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (server >= 0 && !::connect(server, reinterpret_cast<sockaddr*>(&_target), sizeof(_target)))
            pump(ssl.get(), server);
    }
    if (ssl)
        SSL_shutdown(ssl.get());
    if (server >= 0)
        ::close(server);
    ::close(client);
}
//---------------------------------------------------------------------------
void TlsProxy::pump(SSL* ssl, int server)
// Forward both directions until one side is gone
{
    auto client = SSL_get_fd(ssl);
    vector<char> buffer(bufferSize);
    while (!_stop) {
        pollfd events[2] = {{client, POLLIN, 0}, {server, POLLIN, 0}};
        // The pending plaintext of the last record is not visible to poll
        if (!SSL_pending(ssl) && ::poll(events, 2, static_cast<int>(blockingTimeout.count())) <= 0)
            continue;
        if (SSL_pending(ssl) || (events[0].revents & (POLLIN | POLLERR | POLLHUP))) {
            auto received = SSL_read(ssl, buffer.data(), static_cast<int>(buffer.size()));
            if (received <= 0 || !write(server, buffer.data(), static_cast<uint64_t>(received)))
                return;
        }
        if (events[1].revents & (POLLIN | POLLERR | POLLHUP)) {
            auto received = ::recv(server, buffer.data(), buffer.size(), 0);
            if (received > 0) {
                if (SSL_write(ssl, buffer.data(), static_cast<int>(received)) != received)
                    return;
            } else if (!received || !retryable()) {
                return;
            }
        }
    }
}
//---------------------------------------------------------------------------
} // namespace anyblob::test
