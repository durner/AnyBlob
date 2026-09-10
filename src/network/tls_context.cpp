#include "network/tls_context.hpp"
#include "network/tls_connection.hpp"
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
static int newSession(SSL* ssl, SSL_SESSION* session)
// Keeps a session the server just issued, tls 1.3 sends its ticket after the handshake
{
    auto connection = static_cast<TLSConnection*>(SSL_get_ex_data(ssl, TLSContext::connectionSlot()));
    if (!connection)
        return 0;
    return connection->getContext().cacheSession(connection->getHostname(), connection->getPort(), connection->verifiesPeer(), session);
}
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
        SSL_CTX_sess_set_new_cb(_ctx, newSession);

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
        for (auto* session : entry.second.sessions)
            SSL_SESSION_free(session);
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
int TLSContext::connectionSlot()
// The ssl slot that points back to the connection
{
    static int slot = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    return slot;
}
//---------------------------------------------------------------------------
bool TLSContext::cacheSession(const string& hostname, uint32_t port, bool verifyPeer, SSL_SESSION* session)
// Caches the SSL session, takes ownership of the session when it is kept
{
    if (!session)
        return false;

    auto& entry = _sessionCache[hostname];
    // The endpoint moved or changed its verification, its tickets are worthless
    if (entry.port != port || entry.verifyPeer != verifyPeer) {
        for (auto* cached : entry.sessions)
            SSL_SESSION_free(cached);
        entry.sessions.clear();
        entry.port = port;
        entry.verifyPeer = verifyPeer;
    }
    if (entry.sessions.size() >= maxSessionsPerEndpoint)
        return false;
    entry.sessions.push_back(session);
    return true;
}
//---------------------------------------------------------------------------
bool TLSContext::dropSession(const string& hostname, uint32_t port)
// Drops the SSL sessions of the endpoint
{
    auto it = _sessionCache.find(hostname);
    if (it == _sessionCache.end() || it->second.port != port)
        return false;
    for (auto* session : it->second.sessions)
        SSL_SESSION_free(session);
    _sessionCache.erase(it);
    return true;
}
//---------------------------------------------------------------------------
bool TLSContext::reuseSession(const string& hostname, uint32_t port, bool verifyPeer, SSL* ssl)
// Reuses the SSL session
{
    auto it = _sessionCache.find(hostname);
    if (it == _sessionCache.end() || it->second.port != port || it->second.verifyPeer != verifyPeer || it->second.sessions.empty())
        return false;
    auto session = it->second.sessions.back();
    it->second.sessions.pop_back();
    auto reused = SSL_set_session(ssl, session) == 1;
    SSL_SESSION_free(session);
    return reused;
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
