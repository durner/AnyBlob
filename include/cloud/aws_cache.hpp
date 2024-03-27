#pragma once
#include "network/cache.hpp"
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
namespace cloud {
//---------------------------------------------------------------------------
/// Implements the AWS cache logic
class AWSCache : public network::Cache {
    /// The good mtu cache
    std::unordered_map<unsigned, bool> _mtuCache;

    public:
    /// The constructor
    explicit AWSCache(unsigned cacheEntries);
    /// The address resolving
    virtual const addrinfo* resolve(std::string hostname, std::string port, bool& reuse) override;
    /// The destructor
    virtual ~AWSCache() noexcept = default;
};
//---------------------------------------------------------------------------
}; // namespace cloud
//---------------------------------------------------------------------------
}; // namespace anyblob
