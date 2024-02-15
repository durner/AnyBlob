#pragma once
#include "network/http_request.hpp"
#include <memory>
#include <string>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace cloud {
//---------------------------------------------------------------------------
/// Implements the AWS S3 Signing Logic
/// It follows the v4 docu: https://docs.aws.amazon.com/general/latest/gr/sigv4_signing.html
class AWSSigner {
    public:
    struct StringToSign {
        /// The canonical request
        network::HttpRequest& request;
        /// The region
        std::string region;
        /// The service
        std::string service;
        /// The request sha
        std::string requestSHA;
        /// The signed headers
        std::string signedHeaders;
        /// The payload hash
        std::string payloadHash;
    };

    /// Creates the canonical request from the input
    static void encodeCanonicalRequest(network::HttpRequest& request, StringToSign& stringToSign, const uint8_t* bodyData = nullptr, uint64_t bodyLength = 0);
    /// Calculates the signature
    [[nodiscard]] static std::string createSignedRequest(const std::string& keyId, const std::string& secret, const StringToSign& stringToSign);

    private:
    /// Creates the string to sogn
    [[nodiscard]] static std::string createStringToSign(const StringToSign& stringToSign);
};
//---------------------------------------------------------------------------
}; // namespace cloud
}; // namespace anyblob
