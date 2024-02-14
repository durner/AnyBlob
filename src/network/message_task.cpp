#include "network/message_task.hpp"
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
using namespace std;
//---------------------------------------------------------------------------
MessageTask::MessageTask(OriginalMessage* message, ConnectionManager::TCPSettings& tcpSettings, uint32_t chunkSize) : originalMessage(message), tcpSettings(tcpSettings), sendBufferOffset(0), receiveBufferOffset(0), chunkSize(chunkSize), failures(0)
// The constructor
{
}
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
