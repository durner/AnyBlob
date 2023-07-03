#include "network/transaction.hpp"
#include "network/original_message.hpp"
#include "network/tasked_send_receiver.hpp"
#include "utils/data_vector.hpp"
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
using namespace std;
//---------------------------------------------------------------------------
void Transaction::processSync(TaskedSendReceiver& sendReceiver)
// Processes the request messages
{
    do {
        // send the original request message
        for (; _messageCounter < _messages.size(); _messageCounter++) {
            sendReceiver.sendSync(_messages[_messageCounter].get());
        }

        for (auto& multipart : _multipartUploads) {
            if (multipart.state == MultipartUpload::State::Sending) {
                for (auto i = 0ull; i < multipart.eTags.size(); i++)
                    sendReceiver.sendSync(multipart.messages[i].get());
                multipart.state = MultipartUpload::State::Default;
            } else if (multipart.state == MultipartUpload::State::Validating) {
                sendReceiver.sendSync(multipart.messages[multipart.eTags.size()].get());
                multipart.state = MultipartUpload::State::Default;
            }
        }

        // do the download work
        sendReceiver.processSync();
    } while (_multipartUploads.size() != _completedMultiparts);
}
//---------------------------------------------------------------------------
void Transaction::processAsync(TaskedSendReceiverGroup& group)
// Sends the request messages to the task group
{
    // send the original request message
    for (; _messageCounter < _messages.size(); _messageCounter++) {
        while (!group.send(_messages[_messageCounter].get())) {}
    }
    for (auto& multipart : _multipartUploads) {
        if (multipart.state == MultipartUpload::State::Sending) {
            for (auto i = 0ull; i < multipart.eTags.size(); i++)
                while (!group.send(multipart.messages[i].get())) {}
            multipart.state = MultipartUpload::State::Default;
        } else if (multipart.state == MultipartUpload::State::Validating) {
            while (!group.send(multipart.messages[multipart.eTags.size()].get())) {}
            multipart.state = MultipartUpload::State::Default;
        }
    }
}
//---------------------------------------------------------------------------
Transaction::Iterator::reference Transaction::Iterator::operator*() const
// Reference
{
    return (*it)->result;
}
//---------------------------------------------------------------------------
Transaction::Iterator::pointer Transaction::Iterator::operator->() const
// Pointer
{
    return &(*it)->result;
}
//---------------------------------------------------------------------------
Transaction::ConstIterator::reference Transaction::ConstIterator::operator*() const
// Reference
{
    return (*it)->result;
}
//---------------------------------------------------------------------------
Transaction::ConstIterator::pointer Transaction::ConstIterator::operator->() const
// Pointer
{
    return &(*it)->result;
}
//---------------------------------------------------------------------------
}; // namespace network
}; // namespace anyblob
