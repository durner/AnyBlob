#include "network/cache.hpp"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <arpa/inet.h>
#include <sys/types.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace network {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
string_view Cache::tld(string_view domain)
// String prefix
{
    auto pos = domain.find_last_of('.');
    if (pos != string::npos) {
        pos = domain.substr(0, pos - 1).find_last_of('.');
        if (pos == string::npos)
            return domain;
        else
            return string_view(domain).substr(pos + 1, domain.size());
    }
    return string_view();
}
//---------------------------------------------------------------------------
Cache::Cache(unsigned entries)
// Constructor
{
    _addr.reserve(entries);
    for (auto i = 0u; i < entries; i++) {
        _addr.emplace_back(nullptr, &freeaddrinfo);
    }
    _addrString.resize(entries, {"", 0});
    _addrCtr = 0;
}
//---------------------------------------------------------------------------
const addrinfo* Cache::resolve(string hostname, string port, bool& oldAddress)
// Resolve the request
{
    auto addrPos = _addrCtr % static_cast<unsigned>(_addrString.size());
    auto curCtr = _addrString[addrPos].second--;
    auto hostString = hostname + ":" + port;
    oldAddress = true;
    if (_addrString[addrPos].first.compare(hostString) || curCtr == 0) {
        struct addrinfo hints = {};
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* temp;
        if (getaddrinfo(hostname.c_str(), port.c_str(), &hints, &temp) != 0) {
            throw runtime_error("hostname getaddrinfo error");
        }
        _addr[addrPos].reset(temp);
        _addrString[addrPos] = {move(hostString), 12};
        oldAddress = false;
    }
    return _addr[addrPos].get();
}
//---------------------------------------------------------------------------
}; // namespace network
//---------------------------------------------------------------------------
}; // namespace anyblob
