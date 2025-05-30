#include "utils/data_vector.hpp"
#include "catch2/single_include/catch2/catch.hpp"
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::utils::test {
//---------------------------------------------------------------------------
TEST_CASE("data_vector") {
    DataVector<uint64_t> dv;
    dv.reserve(1);
    *dv.data() = 42;
    REQUIRE(dv.size() == 0);
    dv.resize(1);
    REQUIRE(*dv.cdata() == 42);
    REQUIRE(dv.capacity() == 1);
    dv.resize(2);
    *(dv.data() + 1) = 43;
    REQUIRE(dv.size() == 2);
    REQUIRE(dv.capacity() == 2);
    REQUIRE(*dv.cdata() == 42);
    REQUIRE(*(dv.cdata() + 1) == 43);
    auto dv2 = dv;
    REQUIRE(dv2.size() == 2);
    REQUIRE(dv2.capacity() == 2);
}
//---------------------------------------------------------------------------
} // namespace anyblob::utils::test
