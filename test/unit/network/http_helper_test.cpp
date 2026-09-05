#include "network/http_helper.hpp"
#include "catch2/single_include/catch2/catch.hpp"
#include <memory>
#include <string_view>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network::test {
//---------------------------------------------------------------------------
TEST_CASE("http_helper") {
    SECTION("content length header is case-insensitive") {
        constexpr std::string_view response = "HTTP/1.1 200 OK\r\ncontent-length: 8\r\n\r\nsuccess\n";
        std::unique_ptr<HttpHelper::Info> info;

        REQUIRE(HttpHelper::finished(reinterpret_cast<const uint8_t*>(response.data()), response.size(), info));
        REQUIRE(info->encoding == HttpHelper::Encoding::ContentLength);
        REQUIRE(info->length == 8);
    }

    SECTION("chunked encoding is case-insensitive") {
        constexpr std::string_view response = "HTTP/1.1 200 OK\r\ntransfer-encoding: Chunked\r\n\r\n8\r\nsuccess\n\r\n0\r\n\r\n";
        std::unique_ptr<HttpHelper::Info> info;

        REQUIRE(HttpHelper::finished(reinterpret_cast<const uint8_t*>(response.data()), response.size(), info));
        REQUIRE(info->encoding == HttpHelper::Encoding::ChunkedEncoding);
    }
}
//---------------------------------------------------------------------------
} // namespace anyblob::network::test
