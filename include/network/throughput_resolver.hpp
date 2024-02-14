#pragma once
// The `ThroughputResolver` depends on gnu stdlib associative containers that are
// not supported by libcxx. We remove the throughput resolver in its entirety when
// building with libcxx.
#ifndef ANYBLOB_LIBCXX_COMPAT
#include "network/resolver.hpp"
#include <chrono>
#include <ext/pb_ds/assoc_container.hpp>
#include <ext/pb_ds/tree_policy.hpp>
#include <unordered_map>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
//---------------------------------------------------------------------------
namespace network {
//---------------------------------------------------------------------------
/// Implements the AWS Resolver logic
class ThroughputResolver : public network::Resolver {
    /// Order statistic tree
    typedef __gnu_pbds::tree<
        double,
        __gnu_pbds::null_type,
        std::greater<double>,
        __gnu_pbds::rb_tree_tag,
        __gnu_pbds::tree_order_statistics_node_update>
        map_t;
    /// The order statistic tree
    map_t _throughputTree;
    /// The seconds vector as stable ring buffer
    std::vector<double> _throughput;
    /// The map between fd and start time
    std::unordered_map<int, std::pair<unsigned, std::chrono::steady_clock::time_point>> _fdMap;
    /// The seconds vector iterator
    uint64_t _throughputIterator;
    /// The maximum history
    const unsigned _maxHistory = 128;

    public:
    /// The constructor
    explicit ThroughputResolver(unsigned cacheEntries);
    /// The address resolving
    virtual const addrinfo* resolve(std::string hostname, std::string port, bool& reuse) override;
    /// Start the timing
    virtual void startSocket(int fd) override;
    /// Stop the timing
    virtual void stopSocket(int fd, uint64_t bytes) override;
    /// Clears the used server from the cache
    virtual void shutdownSocket(int fd) override;
    /// The destructor
    virtual ~ThroughputResolver() noexcept = default;
};
//---------------------------------------------------------------------------
}; // namespace network
//---------------------------------------------------------------------------
}; // namespace anyblob
#endif // ANYBLOB_LIBCXX_COMPAT
