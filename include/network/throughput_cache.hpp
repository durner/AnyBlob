#ifdef ANYBLOB_LIBCXX_COMPAT
// The `ThroughputCache` depends on gnu stdlib associative containers that are
// not supported by libcxx. We remove the throughput cache in its entirety when
// building with libcxx.
#error "You must not include throughput_cache.hpp when building with libc++"
#endif
#pragma once
#include "network/cache.hpp"
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
namespace anyblob::network {
//---------------------------------------------------------------------------
/// Implements the throughput-based cache logic
class ThroughputCache : public network::Cache {
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
    std::unordered_map<int, std::chrono::steady_clock::time_point> _fdMap;
    /// The seconds vector iterator
    uint64_t _throughputIterator;
    /// The maximum history
    const unsigned _maxHistory = 128;

    public:
    /// The constructor
    explicit ThroughputCache();
    /// Start the timing and advance to the next cache bucket
    virtual void startSocket(int fd) override;
    /// Stops the socket and either closes the connection or cashes it
    virtual void stopSocket(std::unique_ptr<SocketEntry> socketEntry, uint64_t bytes, unsigned cachedEntries, bool reuseSocket) override;
    /// The destructor
    virtual ~ThroughputCache() = default;
};
//---------------------------------------------------------------------------
} // namespace anyblob::network
//---------------------------------------------------------------------------
