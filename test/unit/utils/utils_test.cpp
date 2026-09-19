#include "utils/utils.hpp"
#include "catch2/single_include/catch2/catch.hpp"
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::utils::test {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
TEST_CASE("utils") {
    constexpr unsigned char key[] = "01234567890123456789012345678901";
    constexpr unsigned char iv[] = "0123456789012345";
    string plain = "AnyBlob - Universal Cloud Object Storage Library";
    uint8_t buffer[512];
    auto len = utils::aesEncrypt(key, iv, reinterpret_cast<const uint8_t*>(plain.data()), plain.length(), buffer, sizeof(buffer));
    uint8_t result[512];
    len = utils::aesDecrypt(key, iv, buffer, len, result, sizeof(result));
    string_view res(reinterpret_cast<char*>(result), len);
    REQUIRE(!plain.compare(res));
}
//---------------------------------------------------------------------------
TEST_CASE("url_parameters") {
    // The encoding of a query parameter has to be reversible
    string key = "dir/sub/file.parquet";
    REQUIRE(utils::encodeUrlParameters(key) == "dir%2Fsub%2Ffile.parquet");
    REQUIRE(utils::decodeUrlParameters(utils::encodeUrlParameters(key)) == key);
    string token = "1ueGcxLPRx1Tr/XYExHnhbYLgveDs2J/wm36Hy4vbOwM=";
    REQUIRE(utils::decodeUrlParameters(utils::encodeUrlParameters(token)) == token);
    REQUIRE(utils::decodeUrlParameters("plain") == "plain");
    // An incomplete escape stays untouched
    REQUIRE(utils::decodeUrlParameters("%2") == "%2");
    REQUIRE(utils::decodeUrlParameters("100%") == "100%");
}
//---------------------------------------------------------------------------
TEST_CASE("base64_padding") {
    for (auto length = 1u; length != 12u; length++) {
        string padding(length, '=');
        CHECK_THROWS(utils::base64Decode(reinterpret_cast<const uint8_t*>(padding.data()), padding.size()));
    }
}
//---------------------------------------------------------------------------
} // namespace anyblob::utils::test
