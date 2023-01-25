#include "utils/ring_buffer.hpp"
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
TEST_CASE("ring_buffer") {
    RingBuffer<uint64_t> rb(2);
    REQUIRE(rb.insert(1) == 0);
    REQUIRE(rb.insert(2) == 1);
    REQUIRE(rb.insert(2) == ~0ull);
    REQUIRE(rb.consume().value() == 1);
    REQUIRE(rb.consume().value() == 2);
    REQUIRE(!rb.consume().has_value());
    uint64_t a[] = {3, 4};
    REQUIRE(rb.insertAll(a) == 2);
    REQUIRE(rb.consume().value() == 3);
    REQUIRE(rb.consume().value() == 4);
    REQUIRE(rb.empty());
}
//---------------------------------------------------------------------------
} // namespace test
} // namespace utils
} // namespace anyblob
