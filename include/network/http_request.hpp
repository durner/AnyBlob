#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2024
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
//---------------------------------------------------------------------------
namespace utils {
template <typename T>
class DataVector;
}
//---------------------------------------------------------------------------
namespace network {
//---------------------------------------------------------------------------
/// Implements an helper to serialize and deserialize http requests
struct HttpRequest {
    /// The method class
    enum class Method : uint8_t {
        GET,
        PUT,
        POST,
        DELETE
    };
    enum class Type : uint8_t {
        HTTP_1_0,
        HTTP_1_1
    };
    /// The queries - need to be RFC 3986 conform
    std::map<std::string, std::string> queries;
    /// The headers - need to be without trailing and leading whitespaces
    std::map<std::string, std::string> headers;
    /// The method
    Method method;
    /// The type
    Type type;
    /// The path - needs to be RFC 3986 conform
    std::string path;

    /// Get the request method
    static constexpr auto getRequestMethod(const Method& method) {
        switch (method) {
            case Method::GET: return "GET";
            case Method::PUT: return "PUT";
            case Method::POST: return "POST";
            case Method::DELETE: return "DELETE";
            default: return "";
        }
    }
    /// Get the request type
    static constexpr auto getRequestType(const Type& type) {
        switch (type) {
            case Type::HTTP_1_0: return "HTTP/1.0";
            case Type::HTTP_1_1: return "HTTP/1.1";
            default: return "";
        }
    }
    /// Serialize the request
    [[nodiscard]] static std::unique_ptr<utils::DataVector<uint8_t>> serialize(const HttpRequest& request);
    /// Deserialize the request
    [[nodiscard]] static HttpRequest deserialize(std::string_view data);
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
