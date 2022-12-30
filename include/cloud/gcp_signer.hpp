#pragma once
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
/// Implements the GCP Signing Logic
/// It follows the v4 docu: https://cloud.google.com/storage/docs/access-control/signing-urls-manually
class GCPSigner {
    public:
    struct Request {
        /// The method
        std::string method;
        /// The type
        std::string type;
        /// The path - needs to be RFC 3986 conform
        std::string path;
        /// The queries - need to be RFC 3986 conform
        std::map<std::string, std::string> queries;
        /// The headers - need to be without trailing and leading whitespaces
        std::map<std::string, std::string> headers;
        /// The signed headers
        std::string signedHeaders;
        /// The payload hash
        std::string payloadHash;
        /// The unowned body data
        const uint8_t* bodyData;
        /// The unowned body length
        uint64_t bodyLength;
    };

    struct StringToSign {
        /// The region
        std::string region;
        /// The service
        std::string service;
    };

    /// Builds the signed url
    static std::string createSignedRequest(const std::string& serviceAccountEmail, const std::string& privateRSA, Request& request, const StringToSign& stringToSign);
};
//---------------------------------------------------------------------------
}; // namespace cloud
}; // namespace anyblob
