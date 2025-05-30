#include "utils/ring_buffer.hpp"
#include "catch2/single_include/catch2/catch.hpp"
#include <thread>
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
TEST_CASE("single_threaded_insert_multi_threaded_consume") {
    RingBuffer<uint64_t> rb(1000);
    for (auto i = 0u; i < 1000u; i++) {
        REQUIRE(rb.insert<true>(i) != ~0ull);
    }
    std::vector<std::thread> threads;
    for (auto i = 0u; i < 10u; i++) {
        threads.push_back(std::thread([&] {
            for (int j = 0; j < 100; j++) {
                REQUIRE(rb.consume<true>().value() < 1000ul);
            }
        }));
    }
    for (auto& th : threads) {
        th.join();
    }
    REQUIRE(!rb.consume().has_value());
}
//---------------------------------------------------------------------------
TEST_CASE("multi_threaded_ring_buffer") {
    RingBuffer<uint64_t> rb(1000);
    std::vector<std::thread> threads;
    for (auto i = 0u; i < 10u; i++) {
        threads.push_back(std::thread([&] {
            for (auto j = 0u; j < 100u; j++) {
                REQUIRE(rb.insert<true>(j) != ~0ull);
            }
        }));
    }
    for (auto& th : threads) {
        th.join();
    }
    for (auto i = 0u; i < 1000u; i++) {
        REQUIRE(rb.consume().value() < 100ul);
    }
    REQUIRE(!rb.consume().has_value());
}
//---------------------------------------------------------------------------
TEST_CASE("multi_threaded_ring_buffer_multi_threaded_consume") {
    RingBuffer<uint64_t> rb(1000);
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
        threads.push_back(std::thread([&] {
            for (auto j = 0u; j < 100u; j++) {
                REQUIRE(rb.insert<true>(j) != ~0ull);
            }
        }));
    }
    for (auto& th : threads) {
        th.join();
    }
    // Consume multi-threaded
    threads.clear();
    for (int i = 0; i < 10; i++) {
        threads.push_back(std::thread([&] {
            for (auto j = 0u; j < 100u; j++) {
                REQUIRE(rb.consume<true>().value() < 100ul);
            }
        }));
    }
    for (auto& th : threads) {
        th.join();
    }
    REQUIRE(!rb.consume().has_value());
}
//---------------------------------------------------------------------------
} // namespace anyblob::utils::test
