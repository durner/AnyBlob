#include "network/http_helper.hpp"
#include "network/http_response.hpp"
#include "catch2/single_include/catch2/catch.hpp"
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
    using namespace std;

    auto deserialize = [](string_view status) {
        string header(status);
        header += "\r\nContent-Length: 0\r\n\r\n";
        return HttpResponse::deserialize(header).code;
    };

    // Every modelled status has to survive a round trip through the parser
    for (auto code = static_cast<uint8_t>(HttpResponse::Code::OK_200); code <= static_cast<uint8_t>(HttpResponse::Code::GATEWAY_TIMEOUT_504); code++) {
        auto expected = static_cast<HttpResponse::Code>(code);
        string status = "HTTP/1.1 ";
        status += HttpResponse::getResponseCode(expected);
        REQUIRE(deserialize(status) == expected);
    }
    REQUIRE(deserialize("HTTP/1.1 418 I'm a teapot") == HttpResponse::Code::UNKNOWN);

    // The transient gateway failures are worth another attempt
    REQUIRE(HttpResponse::checkRetryable(HttpResponse::Code::BAD_GATEWAY_502));
    REQUIRE(HttpResponse::checkRetryable(HttpResponse::Code::GATEWAY_TIMEOUT_504));
    REQUIRE(HttpResponse::checkRetryable(HttpResponse::Code::SLOW_DOWN_503));
    REQUIRE(!HttpResponse::checkRetryable(HttpResponse::Code::NOT_FOUND_404));
    REQUIRE(!HttpResponse::checkRetryable(HttpResponse::Code::UNKNOWN));
}
//---------------------------------------------------------------------------
} // namespace anyblob::network::test
