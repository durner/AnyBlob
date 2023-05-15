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
namespace anyblob {
namespace network {
//---------------------------------------------------------------------------
TLSContext::TLSContext()
// Construct the TLS Context
{
    OpenSSL_add_ssl_algorithms();
    auto method = TLS_client_method();
    SSL_load_error_strings();
    ctx = SSL_CTX_new(method);
}
//---------------------------------------------------------------------------
TLSContext::~TLSContext()
// The desturctor
{
    if (ctx)
        SSL_CTX_free(ctx);
}
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
