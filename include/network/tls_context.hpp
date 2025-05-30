#pragma once
#include <array>
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
    /// The ssl context
    SSL_CTX* _ctx;
    /// The cache size as power of 2
    static constexpr uint8_t cachePower = 8;
    /// The cache mask
    static constexpr uint64_t cacheMask = (~0ull) >> (64 - cachePower);
    /// The session cache
    std::array<std::pair<uint64_t, SSL_SESSION*>, 1ull << cachePower> _sessionCache;

    public:
    /// The constructor
    TLSContext();
    /// The destructor
    ~TLSContext();

    /// Caches the SSL session
    bool cacheSession(int fd, SSL* ssl);
    /// Drops the SSL session
    bool dropSession(int fd);
    /// Reuses a SSL session
    bool reuseSession(int fd, SSL* ssl);

    /// Init the OpenSSL algos and errors
    static void initOpenSSL();

    friend TLSConnection;
};
//---------------------------------------------------------------------------
} // namespace anyblob::network
