#include "utils/unordered_map.hpp"
#include "catch2/single_include/catch2/catch.hpp"
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace utils {
namespace test {
//---------------------------------------------------------------------------
TEST_CASE("unordered_map") {
    UnorderedMap<int, int> ht(10);
    REQUIRE(ht.buckets() == 10);
    auto ret = ht.insert(0, 0);
    REQUIRE(ret->first == 0);
    REQUIRE(ret->second == 0);
    ht.push(1, 1);
    REQUIRE(ht.size() == 2);

    REQUIRE(ht.find(0) != ht.end());
    REQUIRE(ht.find(2) == ht.end());

    REQUIRE(ht.erase(ht.find(0)));
    REQUIRE(ht.size() == 1);
}
//---------------------------------------------------------------------------
} // namespace test
} // namespace utils
} // namespace anyblob
