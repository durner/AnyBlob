#pragma once
#include "network/message_task.hpp"
#include <memory>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace network {
//---------------------------------------------------------------------------
/// Implements a http message roundtrip
struct HTTPMessage : public MessageTask {
    /// The receive chunk size
    uint64_t chunkSize;
    /// The tcp settings
    IOUringSocket::TCPSettings tcpSettings;
    /// HTTP info header
    std::unique_ptr<HTTPHelper::Info> info;

    /// The message excecute callback
    MessageState execute(IOUringSocket& socket) override;
    /// The destructor
    ~HTTPMessage() override = default;
    /// Reset for restart
    void reset(IOUringSocket& socket, bool aborted);

    /// The constructor
    HTTPMessage(OriginalMessage* sendingMessage, uint64_t chunkSize);
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
