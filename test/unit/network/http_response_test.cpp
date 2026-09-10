#include "network/http_response.hpp"
#include "catch2/single_include/catch2/catch.hpp"
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2026
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network::test {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
TEST_CASE("http_response") {
    // A ranged response reports the size of the whole object
    auto ranged = HttpResponse::deserialize("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 3-5/12\r\nContent-Length: 3\r\n\r\nabc");
    REQUIRE(ranged.code == HttpResponse::Code::PARTIAL_CONTENT_206);
    REQUIRE(ranged.getObjectSize() == 12);

    // A suffix range reports it as well
    auto suffix = HttpResponse::deserialize("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 4032-4095/4096\r\nContent-Length: 64\r\n\r\n");
    REQUIRE(suffix.getObjectSize() == 4096);

    // A whole object reports its content length
    auto whole = HttpResponse::deserialize("HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\nhello world!");
    REQUIRE(whole.code == HttpResponse::Code::OK_200);
    REQUIRE(whole.getObjectSize() == 12);

    // An unknown size stays zero
    auto unknown = HttpResponse::deserialize("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-3/*\r\nContent-Length: 4\r\n\r\nabcd");
    REQUIRE(unknown.getObjectSize() == 0);
    auto chunked = HttpResponse::deserialize("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    REQUIRE(chunked.getObjectSize() == 0);
}
//---------------------------------------------------------------------------
} // namespace anyblob::network::test
