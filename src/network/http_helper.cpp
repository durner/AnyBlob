#include "network/http_helper.hpp"
#include <algorithm>
#include <cassert>
#include <cctype>
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
namespace anyblob::network {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
static bool equalsIgnoreCase(string_view lhs, string_view rhs)
// Compares case-insensitive strings
{
    auto sameLetter = [](char l, char r) { return tolower(static_cast<unsigned char>(l)) == tolower(static_cast<unsigned char>(r)); };
    return lhs.size() == rhs.size() && equal(lhs.begin(), lhs.end(), rhs.begin(), sameLetter);
}
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
    auto end = header.find(headerEnd);
    assert(end != string_view::npos);
    info.headerLength = static_cast<uint32_t>(end + headerEnd.size());

    for (auto& keyValue : info.response.headers) {
        if (equalsIgnoreCase(transferEncoding, keyValue.first) && equalsIgnoreCase(chunkedEncoding, keyValue.second)) {
            info.encoding = Encoding::ChunkedEncoding;
        } else if (equalsIgnoreCase(contentLength, keyValue.first) && info.encoding != Encoding::ChunkedEncoding) {
            auto parsed = from_chars(keyValue.second.data(), keyValue.second.data() + keyValue.second.size(), info.length);
            if (parsed.ec != errc() || parsed.ptr != keyValue.second.data() + keyValue.second.size())
                throw runtime_error("Invalid HTTP content length");
            info.encoding = Encoding::ContentLength;
        }
    }

    if (info.encoding == Encoding::Unknown && !HttpResponse::withoutContent(info.response.code))
        throw runtime_error("Unsupported HTTP encoding protocol");

    return info;
}
//---------------------------------------------------------------------------
uint64_t HttpHelper::decodeChunks(uint8_t* data, uint64_t length, const Info& info)
// Decodes a complete chunked body
{
    static constexpr string_view newline = "\r\n";
    string_view sv(reinterpret_cast<const char*>(data), length);

    auto walk = [&](bool compact) -> uint64_t {
        auto read = static_cast<uint64_t>(info.headerLength);
        auto write = read;
        while (true) {
            auto end = sv.find(newline, read);
            if (end == sv.npos)
                return sv.npos;
            uint64_t size = 0;
            auto parsed = from_chars(sv.data() + read, sv.data() + end, size, 16);
            if (parsed.ec != errc() || parsed.ptr != sv.data() + end)
                throw runtime_error("Invalid HTTP chunk size");
            read = end + newline.size();
            if (!size)
                break;
            if (read > length || size > length - read || newline.size() > length - read - size)
                return sv.npos;
            if (sv.substr(read + size, newline.size()) != newline)
                throw runtime_error("Invalid HTTP chunk framing");
            if (compact)
                memmove(data + write, data + read, size);
            write += size;
            read += size + newline.size();
        }
        while (true) {
            auto end = sv.find(newline, read);
            if (end == sv.npos)
                return sv.npos;
            if (end == read)
                return write - info.headerLength;
            read = end + newline.size();
        }
    };

    return walk(false) == sv.npos ? sv.npos : walk(true);
}
//---------------------------------------------------------------------------
string_view HttpHelper::retrieveContent(uint8_t* data, uint64_t length, unique_ptr<Info>& info)
// Retrieve the content without http meta info, note that this changes data
{
    if (!info) {
        info = make_unique<Info>(detect(string_view(reinterpret_cast<const char*>(data), length)));
        if (info->encoding == Encoding::ChunkedEncoding) {
            auto decoded = decodeChunks(data, length, *info);
            info->length = decoded == string_view::npos ? 0 : decoded;
        }
    }
    return string_view(reinterpret_cast<const char*>(data) + info->headerLength, info->boundedLength(length));
}
//---------------------------------------------------------------------------
bool HttpHelper::finished(uint8_t* data, uint64_t length, unique_ptr<Info>& info)
// Detect end / content
{
    string_view sv(reinterpret_cast<const char*>(data), length);
    if (!info) {
        if (sv.find("\r\n\r\n"sv) == sv.npos)
            return false;
        info = make_unique<Info>(detect(sv));
    }
    if (HttpResponse::withoutContent(info->response.code))
        return true;
    switch (info->encoding) {
        case Encoding::ContentLength:
            return length >= info->headerLength + info->length;
        case Encoding::ChunkedEncoding: {
            auto decoded = decodeChunks(data, length, *info);
            if (decoded == sv.npos)
                return false;
            info->length = decoded;
            return true;
        }
        default: {
            info = nullptr;
            throw runtime_error("Unsupported HTTP transfer protocol");
        }
    }
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
