#pragma once
#include "loopback_proxy.hpp"
#include <string>
#include <openssl/types.h>
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
/// This class terminates TLS and relays the plaintext to a target, which gives the tests an encrypted endpoint
class TlsProxy : public LoopbackProxy {
    private:
    /// The size of the relay buffer
    static constexpr uint64_t bufferSize = 64u << 10;

    /// The server context with the generated certificate
    SSL_CTX* _ctx = nullptr;

    public:
    /// The constructor listens on a free loopback port and starts the proxy
    TlsProxy(const std::string& target);
    /// The destructor stops the proxy and closes all sockets
    ~TlsProxy();

    private:
    /// Build the context with a self signed certificate for the loopback
    void createContext();
    /// Terminate TLS for one connection and relay it to the target
    void relay(int client);
    /// Forward both directions until one side is gone
    void pump(SSL* ssl, int server);
};
//---------------------------------------------------------------------------
} // namespace anyblob::test
