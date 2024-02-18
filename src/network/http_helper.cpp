#include "network/http_helper.hpp"
#include <cassert>
#include <charconv>
#include <cstring>
#include <stdexcept>
#include <string>
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
HttpHelper::Info HttpHelper::detect(string_view header)
// Detect the protocol
{
    Info info;
    info.response = HttpResponse::deserialize(header);

    static constexpr string_view transferEncoding = "Transfer-Encoding";
    static constexpr string_view chunkedEncoding = "chunked";
    static constexpr string_view contentLength = "Content-Length";
    static constexpr string_view headerEnd = "\r\n\r\n";

    for (auto& keyValue : info.response.headers) {
        if (transferEncoding == keyValue.first && chunkedEncoding == keyValue.second) {
            info.encoding = Encoding::ChunkedEncoding;
            auto end = header.find(headerEnd);
            assert(end != string_view::npos);
            info.headerLength = static_cast<unsigned>(end) + static_cast<unsigned>(headerEnd.length());
        } else if (contentLength == keyValue.first) {
            info.encoding = Encoding::ContentLength;
            from_chars(keyValue.second.data(), keyValue.second.data() + keyValue.second.size(), info.length);
            auto end = header.find(headerEnd);
            assert(end != string_view::npos);
            info.headerLength = static_cast<unsigned>(end) + static_cast<unsigned>(headerEnd.length());
        }
    }

    if (!info.headerLength)
        throw runtime_error("Unsupported HTTP encoding protocol");

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
            info = nullptr;
            throw runtime_error("Unsupported HTTP transfer protocol");
        }
    }
}
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
