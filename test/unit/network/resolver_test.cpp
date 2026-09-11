#include "catch2/single_include/catch2/catch.hpp"
#include "network/cache.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network::test {
//---------------------------------------------------------------------------
using namespace std;
using namespace std::chrono_literals;
//---------------------------------------------------------------------------
/// Helper to access protected fields
struct CacheTester : Cache {
    using Cache::FailedAddress;
    using Cache::selectAddress;
};
//---------------------------------------------------------------------------
TEST_CASE("cache") {
    array<sockaddr_in, 3> addresses{};
    array<addrinfo, 3> chain{};
    for (auto i = 0u; i < chain.size(); i++) {
        addresses[i].sin_family = AF_INET;
        addresses[i].sin_port = htons(80);
        addresses[i].sin_addr.s_addr = htonl(0x7f000001u + i);
        chain[i].ai_addr = reinterpret_cast<sockaddr*>(&addresses[i]);
        if (i + 1 < chain.size())
            chain[i].ai_next = &chain[i + 1];
    }
    auto failedEntry = [&](unsigned i, chrono::steady_clock::time_point expiry) {
        CacheTester::FailedAddress f;
        memcpy(f.first.data(), chain[i].ai_addr->sa_data, f.first.size());
        f.second = expiry;
        return f;
    };

    auto now = chrono::steady_clock::now();
    vector<CacheTester::FailedAddress> failed;
    // Head is selected
    REQUIRE(CacheTester::selectAddress(chain.data(), failed, now) == chain.data());
    // Next address
    failed.push_back(failedEntry(0, now + 5s));
    REQUIRE(CacheTester::selectAddress(chain.data(), failed, now) == (chain.data() + 1));
    // A fully failed chain falls back to head
    failed.push_back(failedEntry(1, now + 5s));
    failed.push_back(failedEntry(2, now + 5s));
    REQUIRE(CacheTester::selectAddress(chain.data(), failed, now) == chain.data());
    // Expired failures are ignored
    failed[0].second = now - 1s;
    REQUIRE(CacheTester::selectAddress(chain.data(), failed, now) == chain.data());

    Cache cache;
    auto entry = cache.resolve("localhost", 80, false, false);
    REQUIRE(entry->dns);
    REQUIRE(entry->dns->selected);
    cache.shutdownSocket(move(entry), 8);
    entry = cache.resolve("localhost", 80, false, false);
    REQUIRE(entry->dns);
    REQUIRE(entry->dns->selected);
}
//---------------------------------------------------------------------------
} // namespace anyblob::network::test
