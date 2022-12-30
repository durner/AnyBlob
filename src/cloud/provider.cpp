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
bool Provider::retrieveBlobs(network::TaskedSendReceiver& sendReceiver, vector<unique_ptr<Blob>>& blobs) const
// Downloads a number of blobs with the calling thread, changes blobs vector
{
    vector<unique_ptr<network::OriginalMessage>> originalMessages;
    originalMessages.reserve(blobs.size());

    // create the original request message
    for (auto& blob : blobs) {
        auto originalMsg = make_unique<network::OriginalMessage>(getRequest(blob->remotePath, blob->range), getAddress(), getPort(), blob->data.get(), blob->capacity);
        sendReceiver.send(originalMsg.get());
        originalMessages.push_back(move(originalMsg));
    }

    // do the download work
    sendReceiver.sendReceive();

    // retrieve the results
    for (auto i = 0u; i < blobs.size(); i++) {
        auto content = sendReceiver.receive(originalMessages[i].get());
        unique_ptr<network::HTTPHelper::Info> infoPtr;
        network::HTTPHelper::retrieveContent(content->cdata(), content->size(), infoPtr);
        blobs[i]->size = infoPtr->length;
        blobs[i]->offset = infoPtr->headerLength;

        // move the datavector data to the blob if the blob is not used as buffer
        if (!blobs[i]->data.get()) {
            blobs[i]->data = content->transferBuffer();
            blobs[i]->capacity = content->capacity();
        }
    }

    return true;
}
//---------------------------------------------------------------------------
bool Provider::isRemoteFile(const std::string_view fileName) noexcept
// Is it a remote file?
{
    for (auto i = 0u; i < remoteFileCount; i++)
        if (fileName.starts_with(remoteFile[i]))
            return true;

    return false;
}
//---------------------------------------------------------------------------
/// Get a region and bucket name
Provider::RemoteInfo Provider::getRemoteInfo(const std::string& fileName) {
    assert(isRemoteFile(fileName));
    Provider::RemoteInfo info;
    info.provider = CloudService::Local;
    for (auto i = 0u; i < remoteFileCount; i++) {
        if (fileName.starts_with(remoteFile[i])) {
            auto sub = fileName.substr(remoteFile[i].size());
            auto pos = sub.find('/');
            auto bucketRegion = sub.substr(0, pos);
            if (auto colonPos = bucketRegion.find(':'); colonPos != std::string::npos) {
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
unique_ptr<Provider> Provider::makeProvider(const string& filepath, const string& clientEmail, const string& keyFile, network::TaskedSendReceiver* sendReceiver)
/// Create a provider
{
    auto info = anyblob::cloud::Provider::getRemoteInfo(filepath);
    switch (info.provider) {
        case anyblob::cloud::Provider::CloudService::AWS: {
            if (sendReceiver && info.region.empty())
                info.region = anyblob::cloud::AWS::getRegion(*sendReceiver);
            auto aws = make_unique<anyblob::cloud::AWS>(info.bucket, info.region);
            aws->initSecret(*sendReceiver);
            return aws;
        }
        case anyblob::cloud::Provider::CloudService::GCP: {
            if (sendReceiver && info.region.empty())
                info.region = anyblob::cloud::GCP::getRegion(*sendReceiver);
            auto gcp = make_unique<anyblob::cloud::GCP>(info.bucket, info.region, clientEmail, keyFile);
            gcp->initKey();
            return gcp;
        }
        case anyblob::cloud::Provider::CloudService::Azure: {
            auto azure = make_unique<anyblob::cloud::Azure>(info.bucket, clientEmail, keyFile);
            azure->initKey();
            return azure;
        }
        default: {
            throw std::runtime_error("Local requests are still unsupported!");
        }
    }
}
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
