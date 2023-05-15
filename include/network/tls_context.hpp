#pragma once
#include <memory>
#include <openssl/types.h>
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
class TLSConnection;
//---------------------------------------------------------------------------
class TLSContext {
    /// The ssl context
    SSL_CTX* ctx;

    public:
    /// The constructor
    TLSContext();
    /// The destructor
    ~TLSContext();

    friend TLSConnection;
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
