#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <netdb.h>
#include <netinet/tcp.h>
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
class IOUringSocket;
//---------------------------------------------------------------------------
/// The addr resolver and cacher, which is not thread safe
class Resolver {
    protected:
    /// The addr info
    std::vector<std::unique_ptr<addrinfo, decltype(&freeaddrinfo)>> _addr;
    /// The current addr string
    std::vector<std::pair</*addrAndPort=*/std::string, /*cacheCtr=*/int>> _addrString;
    /// The ctr
    unsigned _addrCtr;

    public:
    /// The constructor
    explicit Resolver(unsigned entries);
    /// The address resolving
    virtual const addrinfo* resolve(std::string hostname, std::string port, bool& oldAddress);
    /// Reset the current cache bucket
    void resetBucket() { _addrString[_addrCtr % _addrString.size()].second = 0; }
    /// Start the timing and advance to the next cache bucket
    virtual void startSocket(int /*fd*/) { _addrCtr++; }
    /// Stop the timing
    virtual void stopSocket(int /*fd*/, uint64_t /*bytes*/) {}
    /// Shutdown of the socket should clear the same addresses
    virtual void shutdownSocket(int /*fd*/) {}
    /// The destructor
    virtual ~Resolver() noexcept = default;

    /// Get the tld
    static std::string_view tld(std::string_view domain);

    friend IOUringSocket;
};
//---------------------------------------------------------------------------
}; // namespace network
//---------------------------------------------------------------------------
}; // namespace anyblob
