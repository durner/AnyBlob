#pragma once
#include <memory>
#include <vector>
#include <string>
#include <string_view>
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
struct Config;
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
    static constexpr unsigned remoteFileCount = 6;
    /// The remote prefixes
    static constexpr std::string_view remoteFile[] = {"s3://", "azure://", "gcp://", "oci://", "ibm://", "minio://"};
    /// Are we currently testing the provdiers
    static bool testEnviornment;

    /// The cloud service enum
    enum class CloudService : uint8_t {
        AWS = 0,
        Azure = 1,
        GCP = 2,
        Oracle = 3,
        IBM = 4,
        MinIO = 5,
        Local = 255
    };

    /// RemoteInfo struct
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
        uint32_t port = 80;
        /// Is zonal endpoint?
        bool zonal = false;
    };

    /// Instance struct
    struct Instance {
        /// The instance type
        std::string type;
        /// The memory in GB
        double memory;
        /// Number of vCPus
        unsigned vcpu;
        /// The network performance in Mbit/s
        uint64_t network;
    };

    protected:
    CloudService _type;
    /// Builds the http request for downloading a blob or listing a directory
    [[nodiscard]] virtual std::unique_ptr<utils::DataVector<uint8_t>> getRequest(const std::string& filePath, const std::pair<uint64_t, uint64_t>& range) const = 0;
    /// Builds the http request for putting an object without the actual data (header only according to the data and length provided)
    [[nodiscard]] virtual std::unique_ptr<utils::DataVector<uint8_t>> putRequest(const std::string& filePath, std::string_view object) const = 0;
    /// Builds the http request for deleting an object
    [[nodiscard]] virtual std::unique_ptr<utils::DataVector<uint8_t>> deleteRequest(const std::string& filePath) const = 0;
    /// Get the address of the server
    [[nodiscard]] virtual std::string getAddress() const = 0;
    /// Get the port of the server
    [[nodiscard]] virtual uint32_t getPort() const = 0;

    /// Is multipart upload supported, if size > 0?
    [[nodiscard]] virtual uint64_t multipartUploadSize() const { return 0; }
    /// Builds the http request for putting multipart objects without the object data itself
    [[nodiscard]] virtual std::unique_ptr<utils::DataVector<uint8_t>> putRequestGeneric(const std::string& /*filePath*/, std::string_view /*object*/, uint16_t /*part*/, std::string_view /*uploadId*/) const;
    /// Builds the http request for deleting multipart aborted objects
    [[nodiscard]] virtual std::unique_ptr<utils::DataVector<uint8_t>> deleteRequestGeneric(const std::string& /*filePath*/, std::string_view /*uploadId*/) const;
    /// Builds the http request for creating multipart put objects
    [[nodiscard]] virtual std::unique_ptr<utils::DataVector<uint8_t>> createMultiPartRequest(const std::string& /*filePath*/) const;
    /// Builds the http request for completing multipart put objects
    [[nodiscard]] virtual std::unique_ptr<utils::DataVector<uint8_t>> completeMultiPartRequest(const std::string& /*filePath*/, std::string_view /*uploadId*/, const std::vector<std::string>& /*etags*/, std::string& /*eTagsContent*/) const;

    /// Initialize secret
    virtual void initSecret(network::TaskedSendReceiver& /*sendReceiver*/) {}
    /// Get a local copy of the global secret
    virtual void getSecret() {}

    public:
    /// The destructor
    virtual ~Provider() noexcept = default;
    /// Gets the cloud provider type
    [[nodiscard]] CloudService getType() { return _type; }
    /// Is it a remote file?
    [[nodiscard]] static bool isRemoteFile(std::string_view fileName) noexcept;
    /// Get the path of the parent dir without the remote info
    [[nodiscard]] static std::string getRemoteParentDirectory(std::string fileName) noexcept;
    /// Get a region and bucket name
    [[nodiscard]] static Provider::RemoteInfo getRemoteInfo(const std::string& fileName);
    /// Get the key from a keyFile
    [[nodiscard]] static std::string getKey(const std::string& keyFile);
    /// Get the etag from the upload header
    [[nodiscard]] static std::string getETag(std::string_view header);
    /// Get the upload id from the multipart request body
    [[nodiscard]] static std::string getUploadId(std::string_view body);
    /// Parse a row from csv file
    [[nodiscard]] static std::vector<std::string> parseCSVRow(std::string_view body);

    /// Create a provider (keyId is access email for GCP/Azure)
    [[nodiscard]] static std::unique_ptr<Provider> makeProvider(const std::string& filepath, bool https = true, const std::string& keyId = "", const std::string& keyFile = "", network::TaskedSendReceiver* sendReceiver = nullptr);

    /// Init the resolver for specific provider
    virtual void initResolver(network::TaskedSendReceiver& /*sendReceiver*/) {}
    /// Get the instance infos
    [[nodiscard]] virtual Instance getInstanceDetails(network::TaskedSendReceiver& sendReceiver) = 0;
    /// Get the config
    [[nodiscard]] virtual network::Config getConfig(network::TaskedSendReceiver& sendReceiver);

    friend network::Transaction;
};
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
