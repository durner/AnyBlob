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
/// The addr resolver and cacher
class Resolver {
    protected:
    /// The addr info
    std::vector<std::unique_ptr<addrinfo, decltype(&freeaddrinfo)>> _addr;
    /// The current addr string
    std::vector<std::pair<std::string, int>> _addrString;
    /// The ctr
    uint64_t _addrCtr;

    public:
    /// The constructor
    explicit Resolver(unsigned entries);
    /// The address resolving
    virtual unsigned resolve(std::string hostname, std::string port, bool& oldAddress);
    /// Increment the addr ctr
    virtual void increment() { _addrCtr++; }
    /// Erase the current cache
    virtual void erase() { _addrString[_addrCtr % _addrString.size()].second = 0; }
    /// Start the timing
    virtual void startSocket(int /*fd*/, unsigned /*ipAsInt*/) {}
    /// Stop the timing
    virtual void stopSocket(int /*fd*/, uint64_t /*bytes*/) {}
    /// The destructor
    virtual ~Resolver() noexcept = default;

    /// Get the tld
    static std::string_view tld(std::string const& domain);

    friend IOUringSocket;
};
//---------------------------------------------------------------------------
}; // namespace network
//---------------------------------------------------------------------------
}; // namespace anyblob
