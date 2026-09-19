#include "cloud/http.hpp"
#include "catch2/single_include/catch2/catch.hpp"
#include "cloud/provider.hpp"
#include "utils/data_vector.hpp"
#include <limits>
#include <string>
#include <utility>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2026
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::cloud::test {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
TEST_CASE("http") {
    auto provider = Provider::makeProvider("http://host:9000/dir/file");
    auto p = pair<uint64_t, uint64_t>(numeric_limits<uint64_t>::max(), numeric_limits<uint64_t>::max());

    auto dv = provider->getRequest("dir/file", p);
    auto request = string(reinterpret_cast<char*>(dv->data()), dv->size());
    REQUIRE(request == "GET /dir/file HTTP/1.1\r\nHost: host\r\n\r\n");

    dv = provider->getRequest("a\r\nX-Injected: 1\r\n\r\nGET /other", p);
    auto splitRequest = string(reinterpret_cast<char*>(dv->data()), dv->size());
    CHECK(splitRequest.find("\r\n\r\n") == splitRequest.size() - 4);
}
//---------------------------------------------------------------------------
} // namespace anyblob::cloud::test
