#include "network/tls_context.hpp"
#include <netinet/in.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
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
TLSContext::TLSContext() : _sessionCache()
// Construct the TLS Context
{
    // Set to TLS
    auto method = TLS_client_method();

    // Set up the context
    _ctx = SSL_CTX_new(method);

    // Enable session cache
    SSL_CTX_set_session_cache_mode(_ctx, SSL_SESS_CACHE_CLIENT);
}
//---------------------------------------------------------------------------
TLSContext::~TLSContext()
// The desturctor
{
    // Remove all sessions
    for (uint64_t i = 0ull; i < (1ull << cachePower); i++) {
        if (_sessionCache[i].first && _sessionCache[i].second) {
            SSL_SESSION_free(_sessionCache[i].second);
        }
    }

    // Destroy context
    if (_ctx)
        SSL_CTX_free(_ctx);
}
//---------------------------------------------------------------------------
void TLSContext::initOpenSSL()
// Inits the openssl algos
{
    // Load algos
    OpenSSL_add_ssl_algorithms();
    SSL_load_error_strings();
}
//---------------------------------------------------------------------------
bool TLSContext::cacheSession(int fd, SSL* ssl)
// Caches the SSL session
{
    // Is the session already cached?
    if (SSL_session_reused(ssl))
        return false;

    // Get the IP address for caching
    struct sockaddr_in addr;
    socklen_t addrLen = sizeof(struct sockaddr_in);
    if (!getpeername(fd, reinterpret_cast<struct sockaddr*>(&addr), &addrLen)) {
        if (_sessionCache[addr.sin_addr.s_addr & cacheMask].first) {
            SSL_SESSION_free(_sessionCache[addr.sin_addr.s_addr & cacheMask].second);
        }
        _sessionCache[addr.sin_addr.s_addr & cacheMask] = pair<uint64_t, SSL_SESSION*>(addr.sin_addr.s_addr, SSL_get1_session(ssl));
        return true;
    }
    return false;
}
//---------------------------------------------------------------------------
bool TLSContext::dropSession(int fd)
// Drop the SSL session from cache
{
    struct sockaddr_in addr;
    socklen_t addrLen = sizeof(struct sockaddr_in);
    if (!getpeername(fd, reinterpret_cast<struct sockaddr*>(&addr), &addrLen)) {
        if (_sessionCache[addr.sin_addr.s_addr & cacheMask].first) {
            auto session = _sessionCache[addr.sin_addr.s_addr & cacheMask].second;
            _sessionCache[addr.sin_addr.s_addr & cacheMask].first = 0;
            SSL_SESSION_free(session);
        }
        return true;
    }
    return false;
}
//---------------------------------------------------------------------------
bool TLSContext::reuseSession(int fd, SSL* ssl)
// Reuses the SSL session
{
    struct sockaddr_in addr;
    socklen_t addrLen = sizeof(struct sockaddr_in);
    if (!getpeername(fd, reinterpret_cast<struct sockaddr*>(&addr), &addrLen)) {
        auto& pair = _sessionCache[addr.sin_addr.s_addr & cacheMask];
        if (pair.first == addr.sin_addr.s_addr) {
            SSL_set_session(ssl, pair.second);
            return true;
        }
    }
    return false;
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
