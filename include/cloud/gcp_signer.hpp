#pragma once
#include "network/http_request.hpp"
#include <string>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::cloud {
//---------------------------------------------------------------------------
/// Implements the GCP Signing Logic
/// It follows the v4 docu: https://cloud.google.com/storage/docs/access-control/signing-urls-manually
class GCPSigner {
    public:
    struct StringToSign {
        /// The region
        std::string region;
        /// The service
        std::string service;
        /// The signed headers
        std::string signedHeaders;
    };

    /// Builds the signed url
    [[nodiscard]] static std::string createSignedRequest(const std::string& serviceAccountEmail, const std::string& privateRSA, network::HttpRequest& request, StringToSign& stringToSign);
};
//---------------------------------------------------------------------------
} // namespace anyblob::cloud
