#pragma once
#include "cloud/aws_instances.hpp"
#include "cloud/provider.hpp"
#include "utils/data_vector.hpp"
#include <cassert>
#include <string>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
//---------------------------------------------------------------------------
namespace network {
class TaskedSendReceiver;
}; // namespace network
//---------------------------------------------------------------------------
namespace cloud {
//---------------------------------------------------------------------------
namespace test {
class AWSTester;
}; // namespace test
//---------------------------------------------------------------------------
/// Implements the AWS S3 logic
class AWS : public Provider {
    public:
    /// The settings for AWS requests
    struct Settings {
        /// The bucket name
        std::string bucket;
        /// The aws region
        std::string region;
        /// The custom endpoint
        std::string endpoint;
        /// The port
        int port = 80;
    };

    /// The secret
    struct Secret {
        /// The IAM user
        std::string iamUser;
        /// The key id
        std::string keyId;
        /// The secret
        std::string secret;
        /// The session token
        std::string sessionToken;
        /// The expieration
        int64_t experiation;
    };

    /// The fake AMZ timestamp
    const char* fakeAMZTimestamp = "21000101T000000Z";
    /// The fake IAM timestamp
    const char* fakeIAMTimestamp = "2100-01-01T00:00:00Z";

    protected:
    /// The settings
    Settings _settings;
    /// The secret
    std::unique_ptr<Secret> _secret;
    /// The multipart upload size
    uint64_t _multipartUploadSize = 128ull << 20;

    public:
    /// Get instance details
    [[nodiscard]] Provider::Instance getInstanceDetails(network::TaskedSendReceiver& sendReceiver) override;
    /// Get the region of the instance
    [[nodiscard]] static std::string getInstanceRegion(network::TaskedSendReceiver& sendReceiver);
    /// Init the resolver
    void initResolver(network::TaskedSendReceiver& sendReceiver) override;

    /// The constructor
    explicit AWS(const RemoteInfo& info) : _settings({info.bucket, info.region, info.endpoint, info.port}) {
        // Check for compatible clouds
        assert(info.provider == Provider::CloudService::AWS || info.provider == Provider::CloudService::MinIO || info.provider == Provider::CloudService::Oracle || info.provider == Provider::CloudService::IBM);
        // Requires a bucket
        assert(!info.bucket.empty());
        // If the provider is not AWS, we explicitly require a region or an explicit endpoint for the data (for now)
        assert(info.provider == Provider::CloudService::AWS || (!info.endpoint.empty() || !info.region.empty()));

        _type = info.provider;
    }
    /// The custom endpoint constructor
    AWS(const RemoteInfo& info, const std::string& keyId, const std::string& key) : AWS(info) {
        _secret = std::make_unique<Secret>();
        _secret->keyId = keyId;
        _secret->secret = key;
    }

    private:
    /// Initialize secret
    void initSecret(network::TaskedSendReceiver& sendReceiver) override;
    /// Builds the secret http request
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> downloadIAMUser() const;
    /// Builds the secret http request
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> downloadSecret(std::string_view content);
    /// Update secret
    bool updateSecret(std::string_view content);
    /// Checks whether the keys are still valid
    [[nodiscard]] bool validKeys() const;
    /// Get the settings
    [[nodiscard]] inline Settings getSettings() { return _settings; }
    /// Allows multipart upload if size > 0
    [[nodiscard]] uint64_t multipartUploadSize() const override { return _multipartUploadSize; }

    /// Builds the http request for downloading a blob or listing the directory
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> getRequest(const std::string& filePath, const std::pair<uint64_t, uint64_t>& range) const override;
    /// Builds the http request for putting objects without the object data itself
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> putRequestGeneric(const std::string& filePath, const std::string_view object, uint16_t part, const std::string_view uploadId) const override;
    /// Builds the http request for putting objects without the object data itself
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> putRequest(const std::string& filePath, const std::string_view object) const override {
        return putRequestGeneric(filePath, object, 0, "");
    }
    // Builds the http request for deleting an objects
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> deleteRequest(const std::string& filePath) const override {
        return deleteRequestGeneric(filePath, "");
    }
    /// Builds the http request for deleting objects
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> deleteRequestGeneric(const std::string& filePath, const std::string_view uploadId) const override;
    /// Builds the http request for creating multipart put objects
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> createMultiPartRequest(const std::string& filePath) const override;
    /// Builds the http request for completing multipart put objects
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> completeMultiPartRequest(const std::string& filePath, const std::string_view uploadId, const std::vector<std::string>& etags) const override;

    /// Get the address of the server
    [[nodiscard]] std::string getAddress() const override;
    /// Get the port of the server
    [[nodiscard]] uint32_t getPort() const override;

    /// Builds the info http request
    [[nodiscard]] static std::unique_ptr<utils::DataVector<uint8_t>> downloadInstanceInfo(const std::string& info = "instance-type");
    /// Get the IAM address
    [[nodiscard]] static constexpr const char* getIAMAddress() { return "169.254.169.254"; }
    /// Get the port of the IAM server
    [[nodiscard]] static constexpr uint32_t getIAMPort() { return 80; }

    friend Provider;
    friend test::AWSTester;
};
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
