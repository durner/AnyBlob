#include "utils/unordered_map.hpp"
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
TEST_CASE("unordered_map_multi_threaded") {
    // Insert 1000 elements in 10 threads
    UnorderedMap<int, int> ht(128);
    std::thread t[10];
    for (int i = 0; i < 10; i++) {
        t[i] = std::thread([&ht, i]() {
            for (int j = 0; j < 10; j++) {
                std::ignore = ht.insert(i * 10 + j, i * 10 + j);
            }
        });
    }
    for (int i = 0; i < 10; i++) {
        t[i].join();
    }
    // Find the elements multu-threaded
    for (int i = 0; i < 10; i++) {
        t[i] = std::thread([&ht, i]() {
            for (int j = 0; j < 10; j++) {
                REQUIRE(ht.find(i * 10 + j) != ht.end());
            }
        });
    }
    for (int i = 0; i < 10; i++) {
        t[i].join();
    }
}
//---------------------------------------------------------------------------
TEST_CASE("unordered_map_multi_threaded_delete") {
    UnorderedMap<int, int> ht(128);
    // Insert 1000 elements in 10 threads
    std::thread t[10];
    for (int i = 0; i < 10; i++) {
        t[i] = std::thread([&ht, i]() {
            for (int j = 0; j < 10; j++) {
                std::ignore = ht.insert(i * 10 + j, i * 10 + j);
            }
        });
    }
    for (int i = 0; i < 10; i++) {
        t[i].join();
    }
    // Delete the elements multi-threaded
    for (int i = 0; i < 10; i++) {
        t[i] = std::thread([&ht, i]() {
            for (int j = 0; j < 10; j++) {
                REQUIRE(ht.erase(i * 10 + j));
            }
        });
    }
    for (int i = 0; i < 10; i++) {
        t[i].join();
    }
}
//---------------------------------------------------------------------------
} // namespace test
} // namespace utils
} // namespace anyblob
