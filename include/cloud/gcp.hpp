#pragma once
#include "cloud/gcp_instances.hpp"
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
class GCPTester;
}; // namespace test
//---------------------------------------------------------------------------
/// Implements the GCP logic
class GCP : public Provider {
    public:
    /// The settings for gcp requests
    struct Settings {
        /// The bucket name
        std::string bucket;
        /// The gcp region
        std::string region;
        /// The gcp port
        uint32_t port;
    };

    /// The secret
    struct Secret {
        /// The IAM user
        std::string serviceAccountEmail;
        /// The private key
        std::string privateKey;
    };

    /// The fake AMZ timestamp
    const char* fakeAMZTimestamp = "21000101T000000Z";

    private:
    /// The settings
    Settings _settings;
    /// The secret
    std::unique_ptr<Secret> _secret;

    public:
    /// Get instance details
    [[nodiscard]] Provider::Instance getInstanceDetails(network::TaskedSendReceiver& sendReceiver) override;
    /// Get the region of the instance
    [[nodiscard]] static std::string getInstanceRegion(network::TaskedSendReceiver& sendReceiver);

    /// The constructor
    GCP(const RemoteInfo& info, const std::string& clientEmail, const std::string& key) : _settings({info.bucket, info.region, info.port}) {
        assert(info.provider == Provider::CloudService::GCP);
        _type = info.provider;
        _secret = std::make_unique<Secret>();
        _secret->serviceAccountEmail = clientEmail;
        _secret->privateKey = key;
    }

    private:
    /// Get the settings
    [[nodiscard]] inline Settings getSettings() { return _settings; }
    /// Allows multipart upload if size > 0
    [[nodiscard]] uint64_t multipartUploadSize() const override { return 128ull << 20; }

    /// Builds the http request for downloading a blob or listing the directory
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> getRequest(const std::string& filePath, const std::pair<uint64_t, uint64_t>& range) const override;
    /// Builds the http request for putting objects without the object data itself
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> putRequestGeneric(const std::string& filePath, std::string_view object, uint16_t part, std::string_view uploadId) const override;
    /// Builds the http request for putting objects without the object data itself
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> putRequest(const std::string& filePath, std::string_view object) const override {
        return putRequestGeneric(filePath, object, 0, "");
    }
    // Builds the http request for deleting an objects
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> deleteRequest(const std::string& filePath) const override {
        return deleteRequestGeneric(filePath, "");
    }
    /// Builds the http request for deleting objects
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> deleteRequestGeneric(const std::string& filePath, std::string_view uploadId) const override;
    /// Builds the http request for creating multipart put objects
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> createMultiPartRequest(const std::string& filePath) const override;
    /// Builds the http request for completing multipart put objects
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> completeMultiPartRequest(const std::string& filePath, std::string_view uploadId, const std::vector<std::string>& etags) const override;

    /// Get the address of the server
    [[nodiscard]] std::string getAddress() const override;
    /// Get the port of the server
    [[nodiscard]] uint32_t getPort() const override;

    /// Builds the info http request
    [[nodiscard]] static std::unique_ptr<utils::DataVector<uint8_t>> downloadInstanceInfo(const std::string& info = "machine-type");
    /// Get the IAM address
    [[nodiscard]] static constexpr std::string_view getIAMAddress() {
        return "169.254.169.254";
    }
    /// Get the port of the IAM server
    [[nodiscard]] static constexpr uint32_t getIAMPort() {
        return 80;
    }

    friend Provider;
    friend test::GCPTester;
};
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
