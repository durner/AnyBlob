#include "cloud/provider.hpp"
#include "cloud/aws.hpp"
#include "cloud/azure.hpp"
#include "cloud/gcp.hpp"
#include "network/tasked_send_receiver.hpp"
#include <string>
#include <assert.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace cloud {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
bool Provider::testEnviornment = false;
//---------------------------------------------------------------------------
/// Get the dir name without the path
string Provider::getRemoteParentDirectory(string fileName) noexcept {
    for (auto i = 0u; i < remoteFileCount; i++) {
        if (fileName.starts_with(remoteFile[i])) {
            fileName = fileName.substr(remoteFile[i].size());
            auto pos = fileName.find('/');
            fileName = fileName.substr(pos + 1);
        }
    }

    auto pos = fileName.find_last_of('/');
    return fileName.substr(0, pos + 1);
}
//---------------------------------------------------------------------------
unique_ptr<network::OriginalMessage> Provider::getRequest(const network::Transaction& transaction) const
// Builds the http request and transform it to an OriginalMessage for downloading a blob or listing a directory
{
    return make_unique<network::OriginalMessage>(getRequest(transaction.remotePath, transaction.info.range), getAddress(), getPort(), transaction.data.get(), transaction.capacity);
}
//---------------------------------------------------------------------------
unique_ptr<network::OriginalMessage> Provider::putRequest(const network::Transaction& transaction) const
/// Builds the http request and transform it to an OriginalMessage for putting an object without the actual data (header only according to the data and length provided)
{
    auto originalMsg = make_unique<network::OriginalMessage>(putRequest(transaction.remotePath, transaction.info.object), getAddress(), getPort(), transaction.data.get(), transaction.capacity);
    originalMsg->setPutRequestData(reinterpret_cast<const uint8_t*>(transaction.info.object.data()), transaction.info.object.size());
    return originalMsg;
}
//---------------------------------------------------------------------------
template <typename Callback>
unique_ptr<network::OriginalMessage> Provider::getRequest(const network::Transaction& transaction, Callback&& callback) const
// Builds the http request and transform it to an OriginalMessage for downloading a blob or listing a directory
{
    return make_unique<network::OriginalCallbackMessage<Callback>>(forward<Callback&&>(callback), getRequest(transaction.remotePath, transaction.info.range), getAddress(), getPort(), transaction.data.get(), transaction.capacity);
}
//---------------------------------------------------------------------------
template <typename Callback>
unique_ptr<network::OriginalMessage> Provider::putRequest(const network::Transaction& transaction, Callback&& callback) const
/// Builds the http request and transform it to an OriginalMessage for putting an object without the actual data (header only according to the data and length provided)
{
    auto originalMsg = make_unique<network::OriginalCallbackMessage<Callback>>(forward<Callback&&>(callback), putRequest(transaction.remotePath, transaction.info.object), getAddress(), getPort(), transaction.data.get(), transaction.capacity);
    originalMsg->setPutRequestData(reinterpret_cast<const uint8_t*>(transaction.info.object.data()), transaction.info.object.size());
    return originalMsg;
}
//---------------------------------------------------------------------------
bool Provider::retrieveObject(network::TaskedSendReceiver& sendReceiver, network::Transaction& transaction) const
// Downloads a multiple objects with the calling thread, changes transaction vector
{
    // Create the original request message and send it
    auto originalMessage = getRequest(transaction);
    sendReceiver.send(originalMessage.get());

    // do the download work
    sendReceiver.sendReceive();

    // retrieve the results
    auto content = sendReceiver.receive(originalMessage.get());
    unique_ptr<network::HTTPHelper::Info> infoPtr;
    network::HTTPHelper::retrieveContent(content->cdata(), content->size(), infoPtr);
    transaction.size = infoPtr->length;
    transaction.offset = infoPtr->headerLength;

    // move the datavector data to the transaction if the transaction is not used as buffer
    if (!transaction.data.get()) {
        transaction.data = content->transferBuffer();
        transaction.capacity = content->capacity();
    }

    return true;
}
//---------------------------------------------------------------------------
bool Provider::uploadObject(network::TaskedSendReceiver& sendReceiver, network::Transaction& transaction) const
// Uploads a multiple objects with the calling thread, changes blobs vector
{
    // Create the original message and send it
    auto originalMessage = putRequest(transaction);
    sendReceiver.send(originalMessage.get());

    // do the download work
    sendReceiver.sendReceive();

    // retrieve the results
    auto content = sendReceiver.receive(originalMessage.get());
    unique_ptr<network::HTTPHelper::Info> infoPtr;
    network::HTTPHelper::retrieveContent(content->cdata(), content->size(), infoPtr);
    transaction.size = infoPtr->length;
    transaction.offset = infoPtr->headerLength;

    // move the datavector data to the transaction if the transaction is not used as buffer
    if (!transaction.data.get()) {
        transaction.data = content->transferBuffer();
        transaction.capacity = content->capacity();
    }

    return true;
}
//---------------------------------------------------------------------------
bool Provider::retrieveObjects(network::TaskedSendReceiver& sendReceiver, vector<unique_ptr<network::Transaction>>& transactions) const
// Downloads a multiple objects with the calling thread, changes transaction vector
{
    vector<unique_ptr<network::OriginalMessage>> originalMessages;
    originalMessages.reserve(transactions.size());

    // create the original request message
    for (auto& txn : transactions) {
        auto originalMsg = getRequest(*txn);
        sendReceiver.send(originalMsg.get());
        originalMessages.push_back(move(originalMsg));
    }

    // do the download work
    sendReceiver.sendReceive();

    // retrieve the results
    for (auto i = 0u; i < transactions.size(); i++) {
        auto content = sendReceiver.receive(originalMessages[i].get());
        unique_ptr<network::HTTPHelper::Info> infoPtr;
        network::HTTPHelper::retrieveContent(content->cdata(), content->size(), infoPtr);
        transactions[i]->size = infoPtr->length;
        transactions[i]->offset = infoPtr->headerLength;

        // move the datavector data to the transaction if the transaction is not used as buffer
        if (!transactions[i]->data.get()) {
            transactions[i]->data = content->transferBuffer();
            transactions[i]->capacity = content->capacity();
        }
    }

    return true;
}
//---------------------------------------------------------------------------
bool Provider::uploadObjects(network::TaskedSendReceiver& sendReceiver, vector<unique_ptr<network::Transaction>>& transactions) const
// Uploads a multiple objects with the calling thread, changes blobs vector
{
    vector<unique_ptr<network::OriginalMessage>> originalMessages;
    originalMessages.reserve(transactions.size());

    // create the original request message
    for (auto& txn : transactions) {
        auto originalMsg = putRequest(*txn);
        sendReceiver.send(originalMsg.get());
        originalMessages.push_back(move(originalMsg));
    }

    // do the download work
    sendReceiver.sendReceive();

    // retrieve the results
    for (auto i = 0u; i < transactions.size(); i++) {
        auto content = sendReceiver.receive(originalMessages[i].get());
        unique_ptr<network::HTTPHelper::Info> infoPtr;
        network::HTTPHelper::retrieveContent(content->cdata(), content->size(), infoPtr);
        transactions[i]->size = infoPtr->length;
        transactions[i]->offset = infoPtr->headerLength;

        // move the datavector data to the transaction if the transaction is not used as buffer
        if (!transactions[i]->data.get()) {
            transactions[i]->data = content->transferBuffer();
            transactions[i]->capacity = content->capacity();
        }
    }

    return true;
}
//---------------------------------------------------------------------------
bool Provider::isRemoteFile(const string_view fileName) noexcept
// Is it a remote file?
{
    for (auto i = 0u; i < remoteFileCount; i++)
        if (fileName.starts_with(remoteFile[i]))
            return true;

    return false;
}
//---------------------------------------------------------------------------
/// Get a region and bucket name
Provider::RemoteInfo Provider::getRemoteInfo(const string& fileName) {
    assert(isRemoteFile(fileName));
    Provider::RemoteInfo info;
    info.provider = CloudService::Local;
    for (auto i = 0u; i < remoteFileCount; i++) {
        if (fileName.starts_with(remoteFile[i])) {
            /// Handle cloud provider the same except MinIO includes endpoint
            auto sub = fileName.substr(remoteFile[i].size());
            if (!remoteFile[i].compare("minio://")) {
                auto pos = sub.find('/');
                auto addressPort = sub.substr(0, pos);
                if (auto colonPos = addressPort.find(':'); colonPos != string::npos) {
                    info.endpoint = addressPort.substr(0, colonPos);
                    info.port = atoi(addressPort.substr(colonPos + 1).c_str());
                } else {
                    info.endpoint = addressPort;
                    info.port = 80;
                }
                sub = sub.substr(pos + 1);
            }
            auto pos = sub.find('/');
            auto bucketRegion = sub.substr(0, pos);
            if (auto colonPos = bucketRegion.find(':'); colonPos != string::npos) {
                info.bucket = bucketRegion.substr(0, colonPos);
                info.region = bucketRegion.substr(colonPos + 1);
            } else {
                info.bucket = bucketRegion;
                info.region = "";
            }
            info.provider = static_cast<CloudService>(i);
        }
    }
    return info;
}
//---------------------------------------------------------------------------
string Provider::getKey(const string& keyFile)
/// Gets the key from the kefile
{
    ifstream ifs(keyFile);
    return string((istreambuf_iterator<char>(ifs)), (istreambuf_iterator<char>()));
}
//---------------------------------------------------------------------------
unique_ptr<Provider> Provider::makeProvider(const string& filepath, const string& keyId, const string& secret, network::TaskedSendReceiver* sendReceiver)
/// Create a provider
{
    auto info = anyblob::cloud::Provider::getRemoteInfo(filepath);
    switch (info.provider) {
        case anyblob::cloud::Provider::CloudService::AWS: {
            if (sendReceiver && info.region.empty())
                info.region = anyblob::cloud::AWS::getInstanceRegion(*sendReceiver);

            if (keyId.empty()) {
                auto aws = make_unique<anyblob::cloud::AWS>(info);
                aws->initSecret(*sendReceiver);
                return aws;
            }
            auto aws = make_unique<anyblob::cloud::AWS>(info, keyId, secret);
            return aws;
        }
        case anyblob::cloud::Provider::CloudService::GCP: {
            if (sendReceiver && info.region.empty())
                info.region = anyblob::cloud::GCP::getInstanceRegion(*sendReceiver);
            auto gcp = make_unique<anyblob::cloud::GCP>(info, keyId, secret);
            return gcp;
        }
        case anyblob::cloud::Provider::CloudService::Azure: {
            auto azure = make_unique<anyblob::cloud::Azure>(info, keyId, secret);
            return azure;
        }
        case anyblob::cloud::Provider::CloudService::MinIO: {
            auto minio = make_unique<anyblob::cloud::AWS>(info, keyId, secret);
            return minio;
        }
        default: {
            throw runtime_error("Local requests are still unsupported!");
        }
    }
}
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
