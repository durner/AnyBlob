#pragma once
#include "network/http_request.hpp"
#include <map>
#include <memory>
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
namespace cloud {
//---------------------------------------------------------------------------
/// Implements the Azure Signing Logic
/// It follows the v4 docu: https://cloud.google.com/storage/docs/access-control/signing-urls-manually
class AzureSigner {
    public:
    /// Builds the signed url
    [[nodiscard]] static std::string createSignedRequest(const std::string& serviceAccountEmail, const std::string& privateRSA, network::HttpRequest& request);
};
//---------------------------------------------------------------------------
}; // namespace cloud
}; // namespace anyblob
