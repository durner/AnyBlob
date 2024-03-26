#pragma once
#include "cloud/azure_instances.hpp"
#include "cloud/provider.hpp"
#include <cassert>
#include <string>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
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
class AzureTester;
}; // namespace test
//---------------------------------------------------------------------------
/// Implements the Azure logic
class Azure : public Provider {
    public:
    /// The settings for azure requests
    struct Settings {
        /// The container name
        std::string container;
        /// The port
        uint32_t port;
    };

    /// The secret
    struct Secret {
        /// The IAM user
        std::string accountName;
        /// The private key
        std::string privateKey;
    };

    /// The fake XMS timestamp
    const char* fakeXMSTimestamp = "Fri, 01 Jan 2100 00:00:00 GMT";

    private:
    /// The settings
    Settings _settings;
    /// The secret
    std::unique_ptr<Secret> _secret;

    /// Inits key from file
    void initKey();

    public:
    /// Get instance details
    [[nodiscard]] Provider::Instance getInstanceDetails(network::TaskedSendReceiverHandle& sendReceiver) override;
    /// Get the region of the instance
    [[nodiscard]] static std::string getRegion(network::TaskedSendReceiverHandle& sendReceiver);

    /// The constructor
    Azure(const RemoteInfo& info, const std::string& clientEmail, const std::string& key) : _settings({info.bucket, info.port}) {
        assert(info.provider == Provider::CloudService::Azure);
        _type = info.provider;
        _secret = std::make_unique<Secret>();
        _secret->accountName = clientEmail;
        _secret->privateKey = key;
        initKey();
    }

    private:
    /// Get the settings
    [[nodiscard]] inline Settings getSettings() { return _settings; }

    /// Builds the http request for downloading a blob or listing the directory
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> getRequest(const std::string& filePath, const std::pair<uint64_t, uint64_t>& range) const override;
    /// Builds the http request for putting objects without the object data itself
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> putRequest(const std::string& filePath, std::string_view object) const override;
    // Builds the http request for deleting an objects
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> deleteRequest(const std::string& filePath) const override;

    /// Get the address of the server
    [[nodiscard]] std::string getAddress() const override;
    /// Get the port of the server
    [[nodiscard]] uint32_t getPort() const override;

    /// Builds the info http request
    [[nodiscard]] static std::unique_ptr<utils::DataVector<uint8_t>> downloadInstanceInfo();
    /// Get the IAM address
    [[nodiscard]] static constexpr std::string_view getIAMAddress() {
        return "169.254.169.254";
    }
    /// Get the port of the IAM server
    [[nodiscard]] static constexpr uint32_t getIAMPort() {
        return 80;
    }

    friend Provider;
    friend test::AzureTester;
};
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
