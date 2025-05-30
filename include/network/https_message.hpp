#pragma once
#include "network/http_message.hpp"
#include "network/tls_connection.hpp"
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
/// Implements a https message roundtrip
struct HTTPSMessage : public HTTPMessage {
    /// The tls layer
    TLSConnection* tlsLayer;
    /// The fd
    int32_t fd;

    /// The constructor
    HTTPSMessage(OriginalMessage* sendingMessage, ConnectionManager::TCPSettings& tcpSettings, uint32_t chunksize);
    /// The destructor
    ~HTTPSMessage() override = default;
    /// The message excecute callback
    MessageState execute(ConnectionManager& connectionManager) override;
    /// Reset for restart
    void reset(ConnectionManager& socket, bool aborted);
};
//---------------------------------------------------------------------------
} // namespace anyblob::network
