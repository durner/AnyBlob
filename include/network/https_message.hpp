#pragma once
#include "network/http_message.hpp"
#include "network/tls_connection.hpp"
#include <memory>
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
class TLSContext;
//---------------------------------------------------------------------------
/// Implements a https message roundtrip
struct HTTPSMessage : public HTTPMessage {
    /// The fd
    int fd;
    /// The TLSLayer
    std::unique_ptr<TLSConnection> tlsLayer;

    /// The message excecute callback
    MessageState execute(IOUringSocket& socket) override;
    /// The destructor
    ~HTTPSMessage() override = default;
    /// Reset for restart
    void reset(IOUringSocket& socket, bool aborted);

    /// The constructor
    HTTPSMessage(OriginalMessage* sendingMessage, TLSContext& context, uint64_t chunkSize);
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
