#include "network/http_request.hpp"
#include "utils/data_vector.hpp"
#include "catch2/single_include/catch2/catch.hpp"
#include <iostream>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace network {
namespace test {
//---------------------------------------------------------------------------
TEST_CASE("http_request") {
    network::HttpRequest request;

    request.method = network::HttpRequest::Method::GET;
    request.path = "/test";
    request.type = network::HttpRequest::Type::HTTP_1_1;
    request.queries.emplace("key", "value");
    request.queries.emplace("key2", "value2");
    request.headers.emplace("Authorization", "test");
    request.headers.emplace("Timestamp", "2024-02-18 00:00:00");

    auto serialize = network::HttpRequest::serialize(request);
    auto serializeView = std::string_view(reinterpret_cast<char*>(serialize->data()), serialize->size());

    auto deserializedRequest = network::HttpRequest::deserialize(serializeView);

    auto serializeAgain = network::HttpRequest::serialize(deserializedRequest);
    auto serializeAgainView = std::string_view(reinterpret_cast<char*>(serializeAgain->data()), serializeAgain->size());

    REQUIRE(serializeView == serializeAgainView);
}
//---------------------------------------------------------------------------
} // namespace test
} // namespace network
} // namespace anyblob
