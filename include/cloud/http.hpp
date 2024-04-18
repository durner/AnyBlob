#pragma once
#include "cloud/provider.hpp"
#include <cassert>
#include <string>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2024
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
/// Implements a simple http request logic
class HTTP : public Provider {
    public:
    /// The settings for azure requests
    struct Settings {
        /// The container name
        std::string hostname;
        /// The port
        uint32_t port;
    };

    private:
    /// The settings
    Settings _settings;

    public:
    /// The constructor
    HTTP(const RemoteInfo& info) : _settings({info.endpoint, info.port}) {
        assert(info.provider == Provider::CloudService::HTTP || info.provider == Provider::CloudService::HTTPS);
        _type = info.provider;
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
    /// Get the instance details
    [[nodiscard]] Provider::Instance getInstanceDetails(network::TaskedSendReceiverHandle& sendReceiver) override;

    friend Provider;
};
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
