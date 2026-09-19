#pragma once
#include "network/http_response.hpp"
#include <algorithm>
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
namespace anyblob::network {
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

    /// The response metadata
    struct Info {
        /// The response header
        HttpResponse response;
        /// The maximum length
        uint64_t length = 0;
        /// The header length
        uint32_t headerLength = 0;
        /// The encoding
        Encoding encoding = Encoding::Unknown;

        /// Get the available body length
        [[nodiscard]] constexpr uint64_t boundedLength(uint64_t bufferLength) const {
            return bufferLength > headerLength ? std::min(length, bufferLength - headerLength) : 0;
        }
    };

    private:
    /// Detect the protocol
    [[nodiscard]] static Info detect(std::string_view s);
    /// Decode a complete chunked body
    [[nodiscard]] static uint64_t decodeChunks(uint8_t* data, uint64_t length, const Info& info);

    public:
    /// Retrieve the content without http meta info, note that this changes data
    [[nodiscard]] static std::string_view retrieveContent(uint8_t* data, uint64_t length, std::unique_ptr<Info>& info);
    /// Detect end / content
    [[nodiscard]] static bool finished(uint8_t* data, uint64_t length, std::unique_ptr<Info>& info);
};
//---------------------------------------------------------------------------
} // namespace anyblob::network
