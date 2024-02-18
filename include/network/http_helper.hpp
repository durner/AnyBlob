#pragma once
#include "network/http_response.hpp"
#include <cstdint>
#include <memory>
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
namespace network {
//---------------------------------------------------------------------------
/// Implements an helper to resolve http requests
class HttpHelper {
    public:
    /// The encoding
    enum class Encoding : uint8_t {
        Unknown,
        ContentLength,
        ChunkedEncoding
    };

    struct Info {
        /// The response header
        HttpResponse response;
        /// The maximum length
        uint64_t length;
        /// The header length
        uint32_t headerLength;
        /// The encoding
        Encoding encoding;
    };

    private:
    /// Detect the protocol
    [[nodiscard]] static Info detect(std::string_view s);

    public:
    /// Retrieve the content without http meta info, note that this changes data
    [[nodiscard]] static std::string_view retrieveContent(const uint8_t* data, uint64_t length, std::unique_ptr<Info>& info);
    /// Detect end / content
    [[nodiscard]] static bool finished(const uint8_t* data, uint64_t length, std::unique_ptr<Info>& info);
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
