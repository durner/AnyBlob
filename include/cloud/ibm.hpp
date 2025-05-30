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
/// Implements the IBM logic using the AWS S3 compatibility API
class IBM : public AWS {
    public:
    /// The constructor
    explicit IBM(const RemoteInfo& info) : AWS(info) {
        assert(info.provider == Provider::CloudService::IBM);
        // COS requires path-style bucket URLs - similar to MinIO, thus we use the endpoint setting
        _settings.endpoint = getAddress();
    }
    /// The custom endpoint constructor
    IBM(const RemoteInfo& info, const std::string& keyId, const std::string& key) : AWS(info, keyId, key) {
        // COS requires path-style bucket URLs - similar to MinIO, thus we use the endpoint setting
        _settings.endpoint = getAddress();
    }
    /// Get the address of the server
    [[nodiscard]] std::string getAddress() const override;
    /// Get the instance details
    [[nodiscard]] Provider::Instance getInstanceDetails(network::TaskedSendReceiverHandle& sendReceiver) override;

    friend Provider;
};
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
