#include "network/http_helper.hpp"
#include <charconv>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <string>
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
using namespace std;
//---------------------------------------------------------------------------
HttpHelper::Info HttpHelper::detect(string_view header)
// Detect the protocol
{
    static constexpr auto unknown = Info{0, 0, Protocol::Unknown, Encoding::Unknown};
    Info info = unknown;

    static constexpr string_view str_http_1_0 = "HTTP/1.0 200 OK";
    static constexpr string_view str_http_1_1 = "HTTP/1.1 200 OK";
    static constexpr string_view str_http_1_1_partial = "HTTP/1.1 206 Partial Content";
    static constexpr string_view str_http_1_1_created = "HTTP/1.1 201 Created";
    static constexpr string_view str_http_1_1_no_content = "HTTP/1.1 204 No Content";
    if (header.starts_with(str_http_1_0)) {
        info.protocol = Protocol::HTTP_1_0_OK;
    } else if (header.starts_with(str_http_1_1)) {
        info.protocol = Protocol::HTTP_1_1_OK;
    } else if (header.starts_with(str_http_1_1_partial)) {
        info.protocol = Protocol::HTTP_1_1_Partial;
    } else if (header.starts_with(str_http_1_1_created)) {
        info.protocol = Protocol::HTTP_1_1_Created;
    } else if (header.starts_with(str_http_1_1_no_content)) {
        info.protocol = Protocol::HTTP_1_1_No_Content;
    }

    static constexpr string_view chunkedEncoding = "Transfer-Encoding: chunked";
    static constexpr string_view contentLength = "Content-Length: ";
    static constexpr string_view headerEnd = "\r\n\r\n";

    if (info.protocol != Protocol::Unknown) {
        if (header.find(chunkedEncoding) != string_view::npos) {
            info.encoding = Encoding::ChunkedEncoding;
            auto end = header.find(headerEnd);
            if (end == string_view::npos) return unknown;
            info.headerLength = static_cast<unsigned>(end) + static_cast<unsigned>(headerEnd.length());
        } else {
            auto pos = header.find(contentLength);
            if (pos != string_view::npos) {
                auto end = header.find("\r\n", pos);
                if (end == string_view::npos) return unknown;
                auto strSize = header.substr(pos + contentLength.size(), end - pos);
                info.encoding = Encoding::ContentLength;
                from_chars(strSize.data(), strSize.data() + strSize.size(), info.length);
            }
            auto end = header.find(headerEnd);
            if (end == string_view::npos) return unknown;
            info.headerLength = static_cast<unsigned>(end) + static_cast<unsigned>(headerEnd.length());
        }
    }

    return info;
}
//---------------------------------------------------------------------------
string_view HttpHelper::retrieveContent(const uint8_t* data, uint64_t length, unique_ptr<Info>& info)
// Retrieve the content without http meta info, note that this changes data
{
    string_view sv(reinterpret_cast<const char*>(data), length);
    if (!info)
        info = make_unique<Info>(detect(sv));
    if (info->encoding == Encoding::ContentLength)
        return string_view(reinterpret_cast<const char*>(data) + info->headerLength, info->length);
    return {};
}
//---------------------------------------------------------------------------
bool HttpHelper::finished(const uint8_t* data, uint64_t length, unique_ptr<Info>& info)
// Detect end / content
{
    if (!info) {
        string_view sv(reinterpret_cast<const char*>(data), length);
        info = make_unique<Info>(detect(sv));
    }
    if (info->protocol != Protocol::Unknown) {
        switch (info->encoding) {
            case Encoding::ContentLength:
                return length >= info->headerLength + info->length;
            case Encoding::ChunkedEncoding: {
                string_view sv(reinterpret_cast<const char*>(data), length);
                info->length = sv.find("0\r\n\r\n"sv);
                bool ret = info->length != sv.npos;
                if (ret)
                    info->length -= info->headerLength;
                return ret;
            }
            default: {
                if (info->protocol == Protocol::HTTP_1_1_No_Content)
                    return true;

                info = nullptr;
                throw runtime_error("Unsupported HTTP transfer protocol");
            }
        };
    } else {
        info = nullptr;
        throw runtime_error("Unsupported protocol");
    }
}
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
