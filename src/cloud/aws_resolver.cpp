#include "cloud/aws_resolver.hpp"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
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
namespace cloud {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
AWSResolver::AWSResolver(unsigned entries) : Resolver(entries), _mtuCache()
// Constructor
{
}
//---------------------------------------------------------------------------
unsigned AWSResolver::resolve(string hostname, string port, bool& oldAddress)
// Resolve the request
{
    auto addrPos = _addrCtr % _addrString.size();
    auto curCtr = _addrString[addrPos].second--;
    auto hostString = hostname + ":" + port;
    // Reuses old address
    oldAddress = true;
    if (_addrString[addrPos].first.compare(hostString) || curCtr == 0) {
        struct addrinfo hints = {};
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* temp;
        if (getaddrinfo(hostname.c_str(), port.c_str(), &hints, &temp)) {
            throw runtime_error("hostname getaddrinfo error");
        }
        _addr[addrPos].reset(temp);
        if (!Resolver::tld(hostname).compare("amazonaws.com")) {
            struct sockaddr_in* p = reinterpret_cast<sockaddr_in*>(_addr[addrPos]->ai_addr);
            int ipAsInt = p->sin_addr.s_addr;
            auto it = _mtuCache.find(ipAsInt);
            if (it != _mtuCache.end()) {
                if (it->second)
                    _addrString[addrPos] = {hostString, numeric_limits<int>::max()};
                else
                    _addrString[addrPos] = {hostString, 12};
            } else {
                char ipv4[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &p->sin_addr, ipv4, INET_ADDRSTRLEN);
                string ip(ipv4);
                string cmd = "timeout 0.01 ping -s 1473 -D " + string(ipv4) + " -c 1 >>/dev/null 2>>/dev/null";
                auto res = system(cmd.c_str());
                if (!res) {
                    _mtuCache.emplace(ipAsInt, true);
                    _addrString[addrPos] = {hostString, numeric_limits<int>::max()};
                } else {
                    _mtuCache.emplace(ipAsInt, false);
                    _addrString[addrPos] = {hostString, 12};
                }
            }
        } else {
            _addrString[addrPos] = {hostString, 12};
        }
        oldAddress = false;
    }
    return addrPos;
}
//---------------------------------------------------------------------------
}; // namespace cloud
//---------------------------------------------------------------------------
}; // namespace anyblob
