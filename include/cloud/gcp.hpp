#pragma once
#include "cloud/gcp_instances.hpp"
#include "cloud/provider.hpp"
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
/// Implements the GCP logic
class GCP : public Provider {
    public:
    /// The settings for gcp requests
    struct Settings {
        /// The bucket name
        std::string bucket;
        /// The gcp region
        std::string region;
    };

    /// The secret
    struct Secret {
        /// The IAM user
        std::string serviceAccountEmail;
        /// The key file
        std::string rsaKeyFile;
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
    Provider::Instance getInstanceDetails(network::TaskedSendReceiver& sendReceiver) override;
    /// Builds the info http request
    static std::unique_ptr<utils::DataVector<uint8_t>> downloadInstanceInfo(const std::string& info = "machine-type");
    /// Get the region of the instance
    static std::string getRegion(network::TaskedSendReceiver& sendReceiver);
    /// Get the IAM address
    static constexpr const char* getIAMAddress() {
        return "169.254.169.254";
    }
    /// Get the port of the IAM server
    static constexpr uint32_t getIAMPort() {
        return 80;
    }

    public:
    /// The constructor
    explicit GCP(Settings settings, const std::string& clientEmail, const std::string& keyFile) : _settings(settings) {
        _type = Provider::CloudService::GCP;
        _secret = std::make_unique<Secret>();
        _secret->serviceAccountEmail = clientEmail;
        _secret->rsaKeyFile = keyFile;
    }
    /// The constructor
    GCP(const std::string& bucket, const std::string& region, const std::string& clientEmail, const std::string& keyFile) : _settings({bucket, region}) {
        _type = Provider::CloudService::GCP;
        _secret = std::make_unique<Secret>();
        _secret->serviceAccountEmail = clientEmail;
        _secret->rsaKeyFile = keyFile;
    }
    /// Get the settings
    Settings getSettings() { return _settings; }

    /// Builds the http request for downloading a blob or listing the directory
    std::unique_ptr<utils::DataVector<uint8_t>> getRequest(const std::string& filePath, std::pair<uint64_t, uint64_t>& range) const override;
    /// Builds the http request for putting objects without the object data itself
    std::unique_ptr<utils::DataVector<uint8_t>> putRequest(const std::string& filePath, const uint8_t* data, const uint64_t length) const override;

    /// Inits key if not already
    void initKey();
    /// Get the address of the server
    std::string getAddress() const override;
    /// Get the port of the server
    uint32_t getPort() const override;
};
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
