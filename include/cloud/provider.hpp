#pragma once
#include "cloud/blob.hpp"
#include "utils/data_vector.hpp"
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
class OriginalMessage;
}; // namespace network
//---------------------------------------------------------------------------
namespace cloud {
//---------------------------------------------------------------------------
/// Implements the cloud provider abstraction
class Provider {
    public:
    /// The remote prefixes count
    static constexpr unsigned remoteFileCount = 3;
    /// The remote prefixes
    static constexpr std::string_view remoteFile[] = {"s3://", "azure://", "gcp://"};
    /// Are we currently testing the provdiers
    static bool testEnviornment;

    /// The cloud service enum
    enum class CloudService : uint8_t {
        AWS = 0,
        Azure = 1,
        GCP = 2,
        Local = 255
    };

    struct RemoteInfo {
        /// The provider
        CloudService provider;
        /// The bucket name
        std::string bucket;
        /// The region name
        std::string region;
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

    public:
    /// The destructor
    virtual ~Provider() = default;
    /// Builds the http request for downloading a blob or listing a directory
    virtual std::unique_ptr<utils::DataVector<uint8_t>> getRequest(const std::string& filePath, std::pair<uint64_t, uint64_t>& range) const = 0;
    /// Builds the http request for putting an object without the actual data (header only according to the data and length provided)
    virtual std::unique_ptr<utils::DataVector<uint8_t>> putRequest(const std::string& filePath, const uint8_t* data, const uint64_t length) const = 0;
    /// Downloads a number of blobs with the calling thread, changes blobs vector
    bool retrieveBlobs(network::TaskedSendReceiver& sendReceiver, std::vector<std::unique_ptr<Blob>>& blobs) const;
    /// Get the instance infos
    virtual Instance getInstanceDetails(network::TaskedSendReceiver& sendReceiver) = 0;
    /// Get the address of the server
    virtual std::string getAddress() const = 0;
    /// Get the port of the server
    virtual uint32_t getPort() const = 0;
    /// Gets the cloud provider type
    CloudService getType() { return _type; }
    /// Is it a remote file?
    static bool isRemoteFile(const std::string_view fileName) noexcept;
    /// Get the path of the parent dir without the remote info
    static std::string getRemoteParentDirectory(std::string fileName) noexcept;
    /// Get a region and bucket name
    static Provider::RemoteInfo getRemoteInfo(const std::string& fileName);
    /// Initialize secret
    virtual void initSecret(network::TaskedSendReceiver& /*sendReceiver*/) {}
    /// Init the resolver for specific provider
    virtual void initResolver(network::TaskedSendReceiver& /*sendReceiver*/) {}
    /// Create a provider
    static std::unique_ptr<Provider> makeProvider(const std::string& filepath, const std::string& clientEmail = "", const std::string& keyFile = "", network::TaskedSendReceiver* sendReceiver = nullptr);
};
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
