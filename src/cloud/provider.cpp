#include "cloud/provider.hpp"
#include "cloud/aws.hpp"
#include "cloud/azure.hpp"
#include "cloud/gcp.hpp"
#include "network/tasked_send_receiver.hpp"
#include "utils/data_vector.hpp"
#include <fstream>
#include <istream>
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
// Get the dir name without the path
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
bool Provider::isRemoteFile(const string_view fileName) noexcept
// Is it a remote file?
{
    for (auto i = 0u; i < remoteFileCount; i++)
        if (fileName.starts_with(remoteFile[i]))
            return true;

    return false;
}
//---------------------------------------------------------------------------
// Get a region and bucket name
Provider::RemoteInfo Provider::getRemoteInfo(const string& fileName) {
    assert(isRemoteFile(fileName));
    Provider::RemoteInfo info;
    info.provider = CloudService::Local;
    for (auto i = 0u; i < remoteFileCount; i++) {
        if (fileName.starts_with(remoteFile[i])) {
            // Handle cloud provider the same except MinIO includes endpoint
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
// Gets the key from the kefile
{
    ifstream ifs(keyFile);
    return string((istreambuf_iterator<char>(ifs)), (istreambuf_iterator<char>()));
}
//---------------------------------------------------------------------------
unique_ptr<Provider> Provider::makeProvider(const string& filepath, const string& keyId, const string& secret, network::TaskedSendReceiver* sendReceiver)
// Create a provider
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
