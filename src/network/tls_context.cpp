#include "network/tls_context.hpp"
#include <openssl/crypto.h>
#include <openssl/ssl.h>
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
TLSContext::TLSContext() : _trustStore(false), _sessionCache()
// Construct the TLS Context
{
    // Set to TLS
    auto method = TLS_client_method();

    // Set up the context
    _ctx = SSL_CTX_new(method);

    if (_ctx) {
        // Enable session cache
        SSL_CTX_set_session_cache_mode(_ctx, SSL_SESS_CACHE_CLIENT);

        // Load the trust store
        _trustStore = SSL_CTX_set_default_verify_paths(_ctx) == 1;
    }
}
//---------------------------------------------------------------------------
TLSContext::~TLSContext()
// The desturctor
{
    // Remove all sessions
    for (auto& entry : _sessionCache) {
        SSL_SESSION_free(entry.second.session);
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
bool TLSContext::cacheSession(const string& hostname, uint32_t port, bool verifyPeer, SSL* ssl)
// Caches the SSL session
{
    // Is the session already cached?
    if (SSL_session_reused(ssl))
        return false;

    auto session = SSL_get1_session(ssl);
    if (!session)
        return false;

    auto& entry = _sessionCache[hostname];
    if (entry.session)
        SSL_SESSION_free(entry.session);
    entry = SessionEntry{session, port, verifyPeer};
    return true;
}
//---------------------------------------------------------------------------
bool TLSContext::dropSession(const string& hostname, uint32_t port)
// Drop the SSL session from cache
{
    auto it = _sessionCache.find(hostname);
    if (it == _sessionCache.end() || it->second.port != port)
        return false;
    SSL_SESSION_free(it->second.session);
    _sessionCache.erase(it);
    return true;
}
//---------------------------------------------------------------------------
bool TLSContext::reuseSession(const string& hostname, uint32_t port, bool verifyPeer, SSL* ssl)
// Reuses the SSL session
{
    auto it = _sessionCache.find(hostname);
    if (it == _sessionCache.end() || it->second.port != port || it->second.verifyPeer != verifyPeer)
        return false;
    return SSL_set_session(ssl, it->second.session) == 1;
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
