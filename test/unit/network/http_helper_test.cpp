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
    {
        string response = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
        unique_ptr<HttpHelper::Info> info;
        REQUIRE(HttpHelper::finished(bytes(response), response.size(), info));
        CHECK(HttpHelper::retrieveContent(bytes(response), response.size(), info) == "hello");
    }

    {
        string partial = "HTTP/1.1 200 OK\r\nContent-Len";
        unique_ptr<HttpHelper::Info> info;
        CHECK_NOTHROW(HttpHelper::finished(bytes(partial), partial.size(), info));
    }

    {
        string response = "HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\nhello";
        unique_ptr<HttpHelper::Info> info;
        CHECK_THROWS(HttpHelper::finished(bytes(response), response.size(), info));
    }

    {
        string response = "HTTP/1.1 200 OK\r\ncontent-length: 5\r\n\r\nhello";
        unique_ptr<HttpHelper::Info> info;
        CHECK_NOTHROW(HttpHelper::finished(bytes(response), response.size(), info));
    }

    {
        string response = "HTTP/1.1 204 No Content\r\nDate: today\r\n\r\n";
        unique_ptr<HttpHelper::Info> info;
        REQUIRE(HttpHelper::finished(bytes(response), response.size(), info));
        CHECK(info->length == 0);
    }
}
//---------------------------------------------------------------------------
TEST_CASE("http_helper_chunked") {
    {
        string response = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n";
        unique_ptr<HttpHelper::Info> info;
        REQUIRE(HttpHelper::finished(bytes(response), response.size(), info));
        CHECK(HttpHelper::retrieveContent(bytes(response), response.size(), info) == "hello");
    }

    {
        string response = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n9\r\nab0\r\n\r\ncd\r\n0\r\n\r\n";
        unique_ptr<HttpHelper::Info> info;
        REQUIRE(HttpHelper::finished(bytes(response), response.size(), info));
        CHECK(HttpHelper::retrieveContent(bytes(response), response.size(), info) == "ab0\r\n\r\ncd");
    }

    {
        string response = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n";
        unique_ptr<HttpHelper::Info> info;
        REQUIRE(HttpHelper::finished(bytes(response), response.size(), info));
        CHECK(info->encoding == HttpHelper::Encoding::ChunkedEncoding);
    }

    {
        string response = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n5\r\nworld\r\n0\r\n\r\n";
        string buffer;
        unique_ptr<HttpHelper::Info> info;
        for (auto i = 0u; i + 1 < response.size(); i++) {
            buffer.push_back(response[i]);
            REQUIRE(!HttpHelper::finished(bytes(buffer), buffer.size(), info));
        }
        buffer.push_back(response.back());
        REQUIRE(HttpHelper::finished(bytes(buffer), buffer.size(), info));
        CHECK(HttpHelper::retrieveContent(bytes(buffer), buffer.size(), info) == "helloworld");
    }
}
//---------------------------------------------------------------------------
TEST_CASE("http_helper_header_limits") {
    {
        string response = "HTTP/1.1 200 OK\r\nX-Pad: ";
        response.resize(HttpHelper::maxHeaderLength + 1, 'x');
        unique_ptr<HttpHelper::Info> info;
        CHECK_THROWS_AS(HttpHelper::finished(bytes(response), response.size(), info), runtime_error);
    }

    {
        string header = "HTTP/1.1 200 OK\r\n";
        for (auto i = 0u; i < 200u; i++)
            header += "X-Pad-" + to_string(i) + ": 1\r\n";
        header += "Content-Length: 0\r\n\r\n";
        CHECK_THROWS_AS(HttpResponse::deserialize(header), runtime_error);
    }
}
//---------------------------------------------------------------------------
TEST_CASE("http_helper_redirect") {
    string header = "HTTP/1.1 301 Moved Permanently\r\nLocation: http://other/\r\nContent-Length: 0\r\n\r\n";
    CHECK(HttpResponse::deserialize(header).code != HttpResponse::Code::UNKNOWN);
}
//---------------------------------------------------------------------------
TEST_CASE("http_helper_hostile_length") {
    {
        string response = "HTTP/1.1 200 OK\r\nContent-Length: 4096\r\n\r\nhello";
        unique_ptr<HttpHelper::Info> info;
        auto content = HttpHelper::retrieveContent(bytes(response), response.size(), info);
        CHECK(content.size() <= response.size());
    }
}
//---------------------------------------------------------------------------
TEST_CASE("http_helper_status_line") {
    CHECK_THROWS_AS(HttpResponse::deserialize("HTTP/1.1\r\n\r\n"), std::runtime_error);
}
//---------------------------------------------------------------------------
} // namespace anyblob::network::test
