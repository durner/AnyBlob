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
/// Implements an helper to deserialize http responses
struct HttpResponse {
    /// Important error codes
    enum class Code : uint8_t {
        OK_200,
        CREATED_201,
        NO_CONTENT_204,
        PARTIAL_CONTENT_206,
        BAD_REQUEST_400,
        UNAUTHORIZED_401,
        FORBIDDEN_403,
        NOT_FOUND_404,
        CONFLICT_409,
        LENGTH_REQUIRED_411,
        RANGE_NOT_SATISFIABLE_416,
        TOO_MANY_REQUESTS_429,
        INTERNAL_SERVER_ERROR_500,
        SERVICE_UNAVAILABLE_503,
        SLOW_DOWN_503,
        UNKNOWN = 255
    };
    enum class Type : uint8_t {
        HTTP_1_0,
        HTTP_1_1
    };
    /// The headers - need to be without trailing and leading whitespaces
    std::map<std::string, std::string> headers;
    /// The method
    Code code;
    /// The type
    Type type;

    /// Get the request method
    static constexpr auto getResponseCode(const Code& code) noexcept {
        switch (code) {
            case Code::OK_200: return "200 OK";
            case Code::CREATED_201: return "201 Created";
            case Code::NO_CONTENT_204: return "204 No Content";
            case Code::PARTIAL_CONTENT_206: return "206 Partial Content";
            case Code::BAD_REQUEST_400: return "400 Bad Request";
            case Code::UNAUTHORIZED_401: return "401 Unauthorized";
            case Code::FORBIDDEN_403: return "403 Forbidden";
            case Code::NOT_FOUND_404: return "404 Not Found";
            case Code::CONFLICT_409: return "409 Conflict";
            case Code::LENGTH_REQUIRED_411: return "411 Length Required";
            case Code::RANGE_NOT_SATISFIABLE_416: return "416 Range Not Satisfiable";
            case Code::TOO_MANY_REQUESTS_429: return "429 Too Many Requests";
            case Code::INTERNAL_SERVER_ERROR_500: return "500 Internal Server Error";
            case Code::SERVICE_UNAVAILABLE_503: return "503 Service Unavailable";
            case Code::SLOW_DOWN_503: return "503 Slow Down";
            default: return "UNKNOWN";
        }
    }
    /// Get the request type
    static constexpr auto getResponseType(const Type& type) noexcept {
        switch (type) {
            case Type::HTTP_1_0: return "HTTP/1.0";
            case Type::HTTP_1_1: return "HTTP/1.1";
            default: return "UNKNOWN";
        }
    }
    /// Check for successful operation 2xx operations
    static constexpr auto checkSuccess(const Code& code) {
        return (code == Code::OK_200 || code == Code::CREATED_201 || code == Code::NO_CONTENT_204 || code == Code::PARTIAL_CONTENT_206);
    }
    /// Check if the result has no content
    static constexpr auto withoutContent(const Code& code) {
        return code == Code::NO_CONTENT_204;
    }
    /// Deserialize the response
    [[nodiscard]] static HttpResponse deserialize(std::string_view data);
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
