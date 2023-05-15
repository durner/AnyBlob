#pragma once
#include <memory>
#include <vector>
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
class Transaction;
class GetTransaction;
class PutTransaction;
struct OriginalMessage;
}; // namespace network
namespace utils {
template <typename T>
class DataVector;
}; // namespace utils
//---------------------------------------------------------------------------
namespace cloud {
//---------------------------------------------------------------------------
/// Implements the cloud provider abstraction
class Provider {
    public:
    /// The remote prefixes count
    static constexpr unsigned remoteFileCount = 4;
    /// The remote prefixes
    static constexpr std::string_view remoteFile[] = {"s3://", "azure://", "gcp://", "minio://"};
    /// Are we currently testing the provdiers
    static bool testEnviornment;

    /// The cloud service enum
    enum class CloudService : uint8_t {
        AWS = 0,
        Azure = 1,
        GCP = 2,
        MinIO = 3,
        Local = 255
    };

    struct RemoteInfo {
        /// The provider
        CloudService provider;
        /// The bucket name
        std::string bucket = "";
        /// The region name
        std::string region = "";
        /// The endpoint
        std::string endpoint = "";
        /// The port
        int port = 80;
    };

    struct Instance {
        /// The instance type
        std::string type;
        /// The memory in GB
        double memory;
        /// Number of vCPus
        unsigned vcpu;
        /// The network performance in Gbit
        std::string network;
    };

    protected:
    CloudService _type;
    /// Builds the http request for downloading a blob or listing a directory
    [[nodiscard]] virtual std::unique_ptr<utils::DataVector<uint8_t>> getRequest(const std::string& filePath, const std::pair<uint64_t, uint64_t>& range) const = 0;
    /// Builds the http request for putting an object without the actual data (header only according to the data and length provided)
    [[nodiscard]] virtual std::unique_ptr<utils::DataVector<uint8_t>> putRequest(const std::string& filePath, const std::string_view object) const = 0;
    /// Get the address of the server
    [[nodiscard]] virtual std::string getAddress() const = 0;
    /// Get the port of the server
    [[nodiscard]] virtual uint32_t getPort() const = 0;
    /// Initialize secret
    virtual void initSecret(network::TaskedSendReceiver& /*sendReceiver*/) {}

    public:
    /// The destructor
    virtual ~Provider() = default;
    /// Gets the cloud provider type
    [[nodiscard]] CloudService getType() { return _type; }
    /// Is it a remote file?
    [[nodiscard]] static bool isRemoteFile(const std::string_view fileName) noexcept;
    /// Get the path of the parent dir without the remote info
    [[nodiscard]] static std::string getRemoteParentDirectory(std::string fileName) noexcept;
    /// Get a region and bucket name
    [[nodiscard]] static Provider::RemoteInfo getRemoteInfo(const std::string& fileName);
    /// Get the key from a keyFile
    [[nodiscard]] static std::string getKey(const std::string& keyFile);

    /// Create a provider (keyId is access email for GCP/Azure)
    [[nodiscard]] static std::unique_ptr<Provider> makeProvider(const std::string& filepath, bool https = true, const std::string& keyId = "", const std::string& keyFile = "", network::TaskedSendReceiver* sendReceiver = nullptr);

    /// Init the resolver for specific provider
    virtual void initResolver(network::TaskedSendReceiver& /*sendReceiver*/) {}
    /// Get the instance infos
    [[nodiscard]] virtual Instance getInstanceDetails(network::TaskedSendReceiver& sendReceiver) = 0;

    friend network::Transaction;
    friend network::GetTransaction;
    friend network::PutTransaction;
};
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
