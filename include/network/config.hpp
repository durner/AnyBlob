#pragma once
#include <cmath>
#include <cstdint>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2023
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network {
//---------------------------------------------------------------------------
/// Config for number of retriever threads and bandwidth
struct Config {
    /// Default concurrent requests to achieve coreThroughput (based on AWS experiments)
    static constexpr uint64_t defaultCoreConcurrency = 20;
    /// Default throughput per core in Mbit/s (based on AWS experiments)
    /// Per-object bandwidth: 8,000 Mbits / 20 Requests = 400 Mbits / Requests = 50 MiBs / Request
    /// Total requests example: 100,000 Mbits / 400 Mbits = 250 Requests
    static constexpr uint64_t defaultCoreThroughput = 8000;

    /// Throughput per core in Mbit/s
    uint64_t coreThroughput;
    /// Concurrent requests to achieve coreThroughput
    unsigned coreConcurreny;
    /// The network performance in Mbit/s
    uint64_t network;

    /// Get the network bandwidth
    constexpr auto bandwidth() const { return network; }
    /// Get the number of requests per core
    constexpr auto coreRequests() const { return coreConcurreny; }
    /// Get the number of retriever threads to saturate bandwidth
    constexpr auto retrievers() const { return (network + coreThroughput - 1) / coreThroughput; }
    /// Get the total outstanding requests
    constexpr auto totalRequests() const { return retrievers() * coreRequests(); }
};
//---------------------------------------------------------------------------
} // namespace anyblob::network
