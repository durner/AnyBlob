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
namespace anyblob::network {
//---------------------------------------------------------------------------
/// Implements a http message roundtrip
struct HTTPMessage : public MessageTask {
    /// HTTP info header
    std::unique_ptr<HttpHelper::Info> info;

    /// The constructor
    HTTPMessage(OriginalMessage* sendingMessage, ConnectionManager::TCPSettings& tcpSettings, uint32_t chunkSize);
    /// The destructor
    ~HTTPMessage() override = default;
    /// The message excecute callback
    MessageState execute(ConnectionManager& connectionManager) override;
    /// Reset for restart
    void reset(ConnectionManager& connectionManager, bool aborted);
};
//---------------------------------------------------------------------------
} // namespace anyblob::network
