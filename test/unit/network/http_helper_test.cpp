#include "network/http_helper.hpp"
#include "catch2/single_include/catch2/catch.hpp"
#include "network/http_response.hpp"
#include <memory>
#include <stdexcept>
#include <string>
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
using namespace std;
//---------------------------------------------------------------------------
namespace {
//---------------------------------------------------------------------------
/// Test helper
uint8_t* bytes(std::string& response) { return reinterpret_cast<uint8_t*>(response.data()); }
//---------------------------------------------------------------------------
} // namespace
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

//---------------------------------------------------------------------------
TEST_CASE("http_helper_content_length") {
    // A complete response reports its body
    {
        string response = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
        unique_ptr<HttpHelper::Info> info;
        REQUIRE(HttpHelper::finished(bytes(response), response.size(), info));
        CHECK(HttpHelper::retrieveContent(bytes(response), response.size(), info) == "hello");
    }

    // A header that arrives in two pieces needs more bytes, it is not a protocol error
    {
        string partial = "HTTP/1.1 200 OK\r\nContent-Len";
        unique_ptr<HttpHelper::Info> info;
        CHECK_NOTHROW(HttpHelper::finished(bytes(partial), partial.size(), info));
    }

    // A malformed length has to be rejected instead of being used uninitialized
    {
        string response = "HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\nhello";
        unique_ptr<HttpHelper::Info> info;
        CHECK_THROWS(HttpHelper::finished(bytes(response), response.size(), info));
    }

    // Header names are case insensitive, an http/2 origin proxy lowercases them
    {
        string response = "HTTP/1.1 200 OK\r\ncontent-length: 5\r\n\r\nhello";
        unique_ptr<HttpHelper::Info> info;
        CHECK_NOTHROW(HttpHelper::finished(bytes(response), response.size(), info));
    }

    // A response without a body reports no body, its length is never assigned
    {
        string response = "HTTP/1.1 204 No Content\r\nDate: today\r\n\r\n";
        unique_ptr<HttpHelper::Info> info;
        REQUIRE(HttpHelper::finished(bytes(response), response.size(), info));
        CHECK(info->length == 0);
    }
}
//---------------------------------------------------------------------------
TEST_CASE("http_helper_chunked") {
    // A chunked body is delivered like any other
    {
        string response = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n";
        unique_ptr<HttpHelper::Info> info;
        REQUIRE(HttpHelper::finished(bytes(response), response.size(), info));
        CHECK(HttpHelper::retrieveContent(bytes(response), response.size(), info) == "hello");
    }

    // The terminator of the framing is not the same as the bytes of the object
    {
        string response = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n9\r\nab0\r\n\r\ncd\r\n0\r\n\r\n";
        unique_ptr<HttpHelper::Info> info;
        REQUIRE(HttpHelper::finished(bytes(response), response.size(), info));
        CHECK(HttpHelper::retrieveContent(bytes(response), response.size(), info) == "ab0\r\n\r\ncd");
    }

    // Chunked wins over a content length, as the rfc requires
    {
        string response = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n";
        unique_ptr<HttpHelper::Info> info;
        REQUIRE(HttpHelper::finished(bytes(response), response.size(), info));
        CHECK(info->encoding == HttpHelper::Encoding::ChunkedEncoding);
    }
}
//---------------------------------------------------------------------------
TEST_CASE("http_helper_redirect") {
    // A redirect is a protocol answer, not an unparseable status
    string header = "HTTP/1.1 301 Moved Permanently\r\nLocation: http://other/\r\nContent-Length: 0\r\n\r\n";
    CHECK(HttpResponse::deserialize(header).code != HttpResponse::Code::UNKNOWN);
}
//---------------------------------------------------------------------------
TEST_CASE("http_helper_hostile_length") {
    // A length longer than the buffer must not hand out memory past its end
    {
        string response = "HTTP/1.1 200 OK\r\nContent-Length: 4096\r\n\r\nhello";
        unique_ptr<HttpHelper::Info> info;
        // Reading the returned view is a heap overflow, so only its bounds are checked here
        auto content = HttpHelper::retrieveContent(bytes(response), response.size(), info);
        CHECK(content.size() <= response.size());
    }
}
//---------------------------------------------------------------------------
TEST_CASE("http_helper_status_line") {
    // A status line with nothing after the version is malformed, not a crash
    CHECK_THROWS_AS(HttpResponse::deserialize("HTTP/1.1\r\n\r\n"), std::runtime_error);
}
//---------------------------------------------------------------------------
} // namespace anyblob::network::test
