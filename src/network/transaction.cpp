#include "network/transaction.hpp"
#include "network/original_message.hpp"
#include "network/tasked_send_receiver.hpp"
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
using namespace std;
//---------------------------------------------------------------------------
void Transaction::processSync(TaskedSendReceiverHandle& sendReceiverHandle)
// Processes the request messages
{
    assert(sendReceiverHandle.has());
    do {
        // send the original request message
        for (; _messageCounter < _messages.size(); _messageCounter++) {
            sendReceiverHandle.sendSync(_messages[_messageCounter].get());
        }

        for (auto& multipart : _multipartUploads) {
            if (multipart.state == MultipartUpload::State::Sending) {
                for (auto i = 0ull; i < multipart.eTags.size(); i++)
                    sendReceiverHandle.sendSync(multipart.messages[i].get());
                multipart.state = MultipartUpload::State::Default;
            } else if (multipart.state == MultipartUpload::State::Validating) {
                sendReceiverHandle.sendSync(multipart.messages[multipart.eTags.size()].get());
                multipart.state = MultipartUpload::State::Default;
            }
        }

        // do the download work
        sendReceiverHandle.processSync();
    } while (_multipartUploads.size() != _completedMultiparts);
}
//---------------------------------------------------------------------------
bool Transaction::processAsync(TaskedSendReceiverGroup& group)
// Sends the request messages to the task group
{
    // send the original request message
    vector<network::OriginalMessage*> submissions;
    auto multiPartSize = 0ull;
    for (auto& multipart : _multipartUploads) {
        multiPartSize += multipart.messages.size();
    }
    submissions.reserve(_messages.size() + multiPartSize);
    for (; _messageCounter < _messages.size(); _messageCounter++) {
        submissions.emplace_back(_messages[_messageCounter].get());
    }
    for (auto& multipart : _multipartUploads) {
        if (multipart.state == MultipartUpload::State::Sending) {
            for (auto i = 0ull; i < multipart.eTags.size(); i++)
                submissions.emplace_back(multipart.messages[i].get());
        } else if (multipart.state == MultipartUpload::State::Validating) {
            submissions.emplace_back(multipart.messages[multipart.eTags.size()].get());
        }
    }
    auto success = group.send(submissions);
    if (!success) [[unlikely]]
        return success;
    for (auto& multipart : _multipartUploads) {
        if (multipart.state == MultipartUpload::State::Sending || multipart.state == MultipartUpload::State::Validating) {
            multipart.state = MultipartUpload::State::Default;
        }
    }
    return success;
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
} // namespace anyblob::network
