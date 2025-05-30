#pragma once
#include "cloud/aws.hpp"
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
} // namespace network
//---------------------------------------------------------------------------
namespace cloud {
//---------------------------------------------------------------------------
/// Implements the MinIO logic using the AWS S3 compatibility API
class MinIO : public AWS {
    public:
    /// The constructor
    explicit MinIO(const RemoteInfo& info) : AWS(info) {
        assert(info.provider == Provider::CloudService::MinIO);
    }
    /// The custom endpoint constructor
    MinIO(const RemoteInfo& info, const std::string& keyId, const std::string& key) : AWS(info, keyId, key) {}
    /// Get the address of the server
    [[nodiscard]] std::string getAddress() const override;
    /// Get the instance details
    [[nodiscard]] Provider::Instance getInstanceDetails(network::TaskedSendReceiverHandle& sendReceiver) override;
    /// Set the upload split size
    constexpr void setMultipartUploadSize(uint64_t size) { _multipartUploadSize = size; }

    friend Provider;
};
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
