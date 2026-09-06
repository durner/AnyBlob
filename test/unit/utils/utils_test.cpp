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
    auto len = utils::aesEncrypt(key, iv, reinterpret_cast<const uint8_t*>(plain.data()), plain.length(), buffer);
    uint8_t result[512];
    len = utils::aesDecrypt(key, iv, buffer, len, result);
    string_view res(reinterpret_cast<char*>(result), len);
    REQUIRE(!plain.compare(res));

    constexpr string_view hmacKey = "key";
    constexpr string_view message = "The quick brown fox jumps over the lazy dog";
    auto [signature, signatureLength] = utils::hmacSign(reinterpret_cast<const uint8_t*>(hmacKey.data()), hmacKey.size(), reinterpret_cast<const uint8_t*>(message.data()), message.size());
    REQUIRE(signatureLength == 32);
    REQUIRE(utils::hexEncode(signature.get(), signatureLength) == "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
}
//---------------------------------------------------------------------------
} // namespace anyblob::utils::test
