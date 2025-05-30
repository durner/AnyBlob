#include "cloud/provider.hpp"
#include "cloud/aws.hpp"
#include "cloud/azure.hpp"
#include "cloud/gcp.hpp"
#include "cloud/http.hpp"
#include "cloud/ibm.hpp"
#include "cloud/minio.hpp"
#include "cloud/oracle.hpp"
#include "network/config.hpp"
#include "network/tasked_send_receiver.hpp"
#include "utils/data_vector.hpp"
#include <fstream>
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
namespace anyblob::cloud {
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
bool Provider::isRemoteFile(string_view fileName) noexcept
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
            if (!remoteFile[i].compare("http://") || !remoteFile[i].compare("https://") || !remoteFile[i].compare("minio://")) {
                auto pos = sub.find('/');
                auto addressPort = sub.substr(0, pos);
                if (auto colonPos = addressPort.find(':'); colonPos != string::npos) {
                    info.endpoint = addressPort.substr(0, colonPos);
                    auto port = atoi(addressPort.substr(colonPos + 1).c_str());
                    assert(port > 0);
                    info.port = static_cast<unsigned>(port);
                } else {
                    info.endpoint = addressPort;
                    info.port = remoteFile[i].compare("https://") ? 80 : 443;
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
            if (!remoteFile[i].compare("s3://")) {
                // Handle s3 one zone express
                if (info.bucket.size() > 6 && string::npos != info.bucket.find("--x-s3", info.bucket.size() - 7)) {
                    info.zonal = true;
                }
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
string Provider::getETag(string_view header)
// Get the etag from the upload header
{
    string needle = "ETag: \"";
    auto pos = header.find(needle);
    if (pos == header.npos)
        return "";
    pos += needle.length();
    auto end = header.find("\"", pos);
    return string(header.substr(pos, end - pos));
}
//---------------------------------------------------------------------------
string Provider::getUploadId(string_view body)
// Get the upload id from the multipart request body
{
    string needle = "<UploadId>";
    auto pos = body.find(needle);
    if (pos == body.npos)
        return "";
    pos += needle.length();
    auto end = body.find("</UploadId>", pos);
    return string(body.substr(pos, end - pos));
}
//---------------------------------------------------------------------------
vector<string> Provider::parseCSVRow(string_view body)
// Read a csv row (simplified, no quotes in quotes and no new lines)
{
    std::vector<std::string> row;
    row.push_back("");
    auto i = 0ull;
    auto inQuote = false;
    for (char c : body) {
        if (!inQuote) {
            switch (c) {
                case ',':
                    row.push_back("");
                    i++;
                    break;
                case '"':
                    inQuote = true;
                    break;
                default:
                    row[i].push_back(c);
                    break;
            }
        } else {
            if (c == '"')
                inQuote = false;
            else
                row[i].push_back(c);
        }
    }
    return row;
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> Provider::putRequestGeneric(const string& /*filePath*/, string_view /*object*/, uint16_t /*part*/, string_view /*uploadId*/) const
// Builds the http request for putting multipart objects without the object data itself
{
    return nullptr;
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> Provider::deleteRequestGeneric(const string& /*filePath*/, string_view /*uploadId*/) const
// Builds the http request for delete multipart objects
{
    return nullptr;
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> Provider::createMultiPartRequest(const string& /*filePath*/) const
// Builds the http request for creating multipart put objects
{
    return nullptr;
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> Provider::completeMultiPartRequest(const string& /*filePath*/, string_view /*uploadId*/, const vector<string>& /*etags*/, string& /*content*/) const
// Builds the http request for completing multipart put objects
{
    return nullptr;
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> Provider::resignRequest(const utils::DataVector<uint8_t>& /*data*/, const uint8_t* /*bodyData*/, uint64_t /*bodyLength*/) const
// Resigned header
{
    return nullptr;
}
//---------------------------------------------------------------------------
network::Config Provider::getConfig(network::TaskedSendReceiverHandle& sendReceiverHandle)
// Uses the send receiver to get instance configs
{
    auto instance = getInstanceDetails(sendReceiverHandle);
    return network::Config{network::Config::defaultCoreThroughput, network::Config::defaultCoreConcurrency, instance.network};
}
//---------------------------------------------------------------------------
unique_ptr<Provider> Provider::makeProvider(const string& filepath, bool https, const string& keyId, const string& secret, network::TaskedSendReceiverHandle* sendReceiverHandle)
// Create a provider
{
    auto info = anyblob::cloud::Provider::getRemoteInfo(filepath);
    if (https && info.port == 80)
        info.port = 443;
    switch (info.provider) {
        case anyblob::cloud::Provider::CloudService::AWS: {
            if (sendReceiverHandle && info.region.empty())
                info.region = anyblob::cloud::AWS::getInstanceRegion(*sendReceiverHandle);

            if (info.region.empty())
                throw runtime_error("IMDSv1 required for retrieving instance region.");

            if (keyId.empty()) {
                auto aws = make_unique<anyblob::cloud::AWS>(info);
                aws->initSecret(*sendReceiverHandle);
                return aws;
            }
            auto aws = make_unique<anyblob::cloud::AWS>(info, keyId, secret);
            return aws;
        }
        case anyblob::cloud::Provider::CloudService::GCP: {
            if (sendReceiverHandle && info.region.empty())
                info.region = anyblob::cloud::GCP::getInstanceRegion(*sendReceiverHandle);
            auto gcp = make_unique<anyblob::cloud::GCP>(info, keyId, secret);
            return gcp;
        }
        case anyblob::cloud::Provider::CloudService::Azure: {
            auto azure = make_unique<anyblob::cloud::Azure>(info, keyId, secret);
            return azure;
        }
        case anyblob::cloud::Provider::CloudService::IBM: {
            auto ibm = make_unique<anyblob::cloud::IBM>(info, keyId, secret);
            return ibm;
        }
        case anyblob::cloud::Provider::CloudService::Oracle: {
            auto oracle = make_unique<anyblob::cloud::Oracle>(info, keyId, secret);
            return oracle;
        }
        case anyblob::cloud::Provider::CloudService::MinIO: {
            auto minio = make_unique<anyblob::cloud::MinIO>(info, keyId, secret);
            return minio;
        }
        case anyblob::cloud::Provider::CloudService::HTTP: {
            auto http = make_unique<anyblob::cloud::HTTP>(info);
            return http;
        }
        case anyblob::cloud::Provider::CloudService::HTTPS: {
            auto http = make_unique<anyblob::cloud::HTTP>(info);
            return http;
        }
        default: {
            throw runtime_error("Local requests are still unsupported!");
        }
    }
}
//---------------------------------------------------------------------------
} // namespace anyblob::cloud
