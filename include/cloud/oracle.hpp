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
}; // namespace network
//---------------------------------------------------------------------------
namespace cloud {
//---------------------------------------------------------------------------
/// Implements the Oracle logic using the AWS S3 compatibility API
class Oracle : public AWS {
    public:
    /// The constructor
    explicit Oracle(const RemoteInfo& info) : AWS(info) {
        assert(info.provider == Provider::CloudService::Oracle);
    }
    /// The custom endpoint constructor
    Oracle(const RemoteInfo& info, const std::string& keyId, const std::string& key) : AWS(info, keyId, key) {}
    /// Get the address of the server
    [[nodiscard]] std::string getAddress() const override;
    /// Get the instance details
    Provider::Instance getInstanceDetails(network::TaskedSendReceiver& sendReceiver) override;

    friend Provider;
};
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
