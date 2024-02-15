#include "cloud/azure_signer.hpp"
#include "utils/utils.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <map>
#include <sstream>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
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
string AzureSigner::createSignedRequest(const string& accountName, const string& privateRSA, network::HttpRequest& request)
// Creates the canonical request
{
    auto decodedKey = utils::base64Decode(reinterpret_cast<const uint8_t*>(privateRSA.data()), privateRSA.size());

    stringstream requestStream;
    // canonicalize request method
    requestStream << network::HttpRequest::getRequestMethod(request) << "\n";

    // Set the version
    request.headers.emplace("x-ms-version", "2015-02-21");

    auto it = request.headers.find("Content-Encoding");
    if (it != request.headers.end())
        requestStream << it->second + "\n";
    else
        requestStream << "\n";

    it = request.headers.find("Content-Language");
    if (it != request.headers.end())
        requestStream << it->second + "\n";
    else
        requestStream << "\n";

    it = request.headers.find("Content-Length");
    if (it != request.headers.end())
        requestStream << it->second + "\n";
    else
        requestStream << "\n";

    it = request.headers.find("Content-MD5");
    if (it != request.headers.end())
        requestStream << it->second + "\n";
    else
        requestStream << "\n";

    it = request.headers.find("Content-Type");
    if (it != request.headers.end())
        requestStream << it->second + "\n";
    else
        requestStream << "\n";

    it = request.headers.find("Date");
    if (it != request.headers.end())
        requestStream << it->second + "\n";
    else
        requestStream << "\n";

    it = request.headers.find("If-Modified-Since");
    if (it != request.headers.end())
        requestStream << it->second + "\n";
    else
        requestStream << "\n";

    it = request.headers.find("If-Match");
    if (it != request.headers.end())
        requestStream << it->second + "\n";
    else
        requestStream << "\n";

    it = request.headers.find("If-None-Match");
    if (it != request.headers.end())
        requestStream << it->second + "\n";
    else
        requestStream << "\n";

    it = request.headers.find("If-Unmodified-Since");
    if (it != request.headers.end())
        requestStream << it->second + "\n";
    else
        requestStream << "\n";

    it = request.headers.find("Range");
    if (it != request.headers.end())
        requestStream << it->second + "\n";
    else
        requestStream << "\n";

    // canonicalize headers, assume no unnecessary whitespaces in header
    map<string, string> sorted;
    if (request.headers.size()) {
        auto it = request.headers.begin();
        while (it != request.headers.end()) {
            string key = it->first;
            transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return tolower(c); });
            string& val = it->second;
            sorted.emplace(key, val);
            ++it;
        }
        for (auto& h : sorted)
            if (h.first.substr(0, 5) == "x-ms-")
                requestStream << h.first << ":" << h.second << "\n";
    }

    requestStream << "/" + accountName + request.path;

    stringstream query;
    // canonicalize query; assume that all query arguments are RFC 3986 conform
    if (request.queries.size()) {
        requestStream << "\n";
        auto it = request.queries.begin();
        while (it != request.queries.end()) {
            requestStream << it->first << ":" << it->second;
            query << utils::encodeUrlParameters(it->first) << "&" << utils::encodeUrlParameters(it->second);
            if (++it != request.queries.end()) {
                requestStream << "\n";
                query << "&";
            }
        }
    }

    // create sha256 request string
    auto requestString = requestStream.str();

    auto signature = utils::hmacSign(decodedKey.first.get(), decodedKey.second, reinterpret_cast<uint8_t*>(requestString.data()), requestString.size());

    request.headers.emplace("Authorization", "SharedKey " + accountName + ":" + utils::base64Encode(signature.first.get(), signature.second));

    string url = (request.path.empty() ? "/" : request.path);
    if (request.queries.size())
        url = url + "?" + query.str();
    return url;
}
//---------------------------------------------------------------------------
}; // namespace cloud
}; // namespace anyblob
