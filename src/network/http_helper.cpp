#include "network/http_helper.hpp"
#include <charconv>
#include <cstring>
#include <stdexcept>
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
using namespace std;
//---------------------------------------------------------------------------
HTTPHelper::Info HTTPHelper::detect(const string_view header)
/// Detect the protocol
{
    auto info = Info{0, 0, Protocol::Unknown, Encoding::Unknown};

    string str_http_1_0 = "HTTP/1.0 200 OK";
    string str_http_1_1 = "HTTP/1.1 200 OK";
    string str_http_1_1_partial = "HTTP/1.1 206 Partial Content";
    string str_http_1_1_created = "HTTP/1.1 201 Created";

    if (header.length() >= str_http_1_0.size() && !strncmp(header.data(), str_http_1_0.c_str(), str_http_1_0.size()))
        info.protocol = Protocol::HTTP_1_0_OK;
    else if (header.length() >= str_http_1_1.size() && !strncmp(header.data(), str_http_1_1.c_str(), str_http_1_1.size()))
        info.protocol = Protocol::HTTP_1_1_OK;
    else if (header.length() >= str_http_1_1_partial.size() && !strncmp(header.data(), str_http_1_1_partial.c_str(), str_http_1_1_partial.size()))
        info.protocol = Protocol::HTTP_1_1_Partial;
    else if (header.length() >= str_http_1_1_created.size() && !strncmp(header.data(), str_http_1_1_created.c_str(), str_http_1_1_created.size()))
        info.protocol = Protocol::HTTP_1_1_Created;


    if (info.protocol != Protocol::Unknown) {
        if (header.npos != header.find("Transfer-Encoding: chunked")) {
            info.encoding = Encoding::ChunkedEncoding;
        } else {
            string contentLength = "Content-Length: ";
            auto pos = header.find(contentLength);
            if (header.npos != pos) {
                auto end = header.find("\r\n", pos);
                auto strSize = header.substr(pos + contentLength.size(), end - pos);
                info.encoding = Encoding::ContentLength;
                from_chars(strSize.data(), strSize.data() + strSize.size(), info.length);
            }
            info.headerLength = header.find("\r\n\r\n"sv) + 4;
        }
    }

    return info;
}
//---------------------------------------------------------------------------
string_view HTTPHelper::retrieveContent(const uint8_t* data, uint64_t length, unique_ptr<Info>& info)
/// Retrieve the content without http meta info, note that this changes data
{
    string_view sv(reinterpret_cast<const char*>(data), length);
    if (!info)
        info = make_unique<Info>(detect(sv));
    if (info->encoding == Encoding::ContentLength)
        return string_view(reinterpret_cast<const char*>(data) + info->headerLength, info->length);
    return {};
}
//---------------------------------------------------------------------------
bool HTTPHelper::finished(const uint8_t* data, uint64_t length, unique_ptr<Info>& info)
/// Detect end / content
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
                return sv.find("0\r\n\r\n"sv) != sv.npos;
            }
            default: {
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
