#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <openssl/ssl.h>
#include <openssl/types.h>
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
class TLSConnection;
//---------------------------------------------------------------------------
// Although the tls context can be used safe in multi-threading enviornments,
// we allow only one context per thread to avoid locking.
// This simplifies also the caching of sessions.
class TLSContext {
    /// The cached sessions of one endpoint
    struct SessionEntry {
        /// The unused sessions, a ticket resumes at most once
        std::vector<SSL_SESSION*> sessions;
        /// The port
        uint32_t port = 0;
        /// Was the peer verified
        bool verifyPeer = false;
    };

    /// The number of sessions kept per endpoint
    static constexpr unsigned maxSessionsPerEndpoint = 8;

    /// The ssl context
    SSL_CTX* _ctx;
    /// Is the trust store loaded
    bool _trustStore;
    /// The session cache, uses the hostname as key
    std::unordered_map<std::string, SessionEntry> _sessionCache;

    public:
    /// The constructor
    TLSContext();
    /// The destructor
    ~TLSContext();

    /// Is the trust store available
    [[nodiscard]] bool hasTrustStore() const { return _trustStore; }

    /// Caches the SSL session, takes ownership of the session when it is kept
    bool cacheSession(const std::string& hostname, uint32_t port, bool verifyPeer, SSL_SESSION* session);
    /// Drops the SSL sessions of the endpoint
    bool dropSession(const std::string& hostname, uint32_t port);
    /// Reuses a SSL session
    bool reuseSession(const std::string& hostname, uint32_t port, bool verifyPeer, SSL* ssl);

    /// Init the OpenSSL algos and errors
    static void initOpenSSL();
    /// The ssl slot that points back to the connection
    static int connectionSlot();

    friend TLSConnection;
};
//---------------------------------------------------------------------------
} // namespace anyblob::network
