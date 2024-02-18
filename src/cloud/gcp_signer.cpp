#include "cloud/gcp_signer.hpp"
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
string GCPSigner::createSignedRequest(const string& serviceAccountEmail, const string& privateRSA, network::HttpRequest& request, StringToSign& stringToSign)
// Creates the canonical request
{
    stringstream requestStream;
    // canonicalize request method
    requestStream << network::HttpRequest::getRequestMethod(request.method) << "\n";

    // canonicalize request path; assume that path is RFC 3986 conform
    if (request.path.empty())
        requestStream << "/\n";
    else
        requestStream << request.path << "\n";

    // canonicalize headers, assume no unnecessary whitespaces in header
    map<string, string> sorted;
    stringstream headers;
    if (request.headers.size()) {
        auto it = request.headers.begin();
        while (it != request.headers.end()) {
            string key = it->first;
            transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return tolower(c); });
            sorted.emplace(key, it->second);
            ++it;
        }
        for (auto& h : sorted)
            headers << h.first << ":" << h.second << "\n";
    }

    if (sorted.size()) {
        stringstream signedRequests;
        auto it = sorted.begin();
        while (it != sorted.end()) {
            signedRequests << it->first;
            if (++it != sorted.end())
                signedRequests << ";";
        }
        stringToSign.signedHeaders = signedRequests.str();
    }
    sorted.clear();

    auto it = request.queries.find("X-Goog-Date");
    if (it == request.headers.end())
        throw runtime_error("missing X-Goog-Date");

    stringstream credentialScope;
    credentialScope << it->second.substr(0, 8) << "/" << stringToSign.region << "/" << stringToSign.service << "/goog4_request";

    request.queries.emplace("X-Goog-Algorithm", "GOOG4-RSA-SHA256");
    request.queries.emplace("X-Goog-Credential", serviceAccountEmail + "/" + credentialScope.str());
    request.queries.emplace("X-Goog-Expires", "3600");
    request.queries.emplace("X-Goog-SignedHeaders", stringToSign.signedHeaders);

    if (request.queries.size()) {
        auto it = request.queries.begin();
        while (it != request.queries.end()) {
            sorted.emplace(utils::encodeUrlParameters(it->first), utils::encodeUrlParameters(it->second));
            ++it;
        }
    }

    request.queries.swap(sorted);

    stringstream query;
    // canonicalize query; assume that all query arguments are RFC 3986 conform
    if (request.queries.size()) {
        auto it = request.queries.begin();
        while (it != request.queries.end()) {
            query << it->first << "=" << it->second;
            if (++it != request.queries.end())
                query << "&";
        }
    }
    requestStream << query.str() << "\n";
    requestStream << headers.str() << "\n";
    requestStream << stringToSign.signedHeaders << "\n";
    requestStream << "UNSIGNED-PAYLOAD";

    // create sha256 request string and return both the request and the sha256 request string
    auto requestString = requestStream.str();
    auto requestHash = utils::sha256Encode(reinterpret_cast<uint8_t*>(requestString.data()), requestString.length());

    it = request.queries.find("X-Goog-Date");
    if (it == request.headers.end())
        throw runtime_error("missing X-Goog-Date");

    stringstream stringToSignStream;
    stringToSignStream << "GOOG4-RSA-SHA256\n";
    stringToSignStream << it->second << "\n";
    stringToSignStream << credentialScope.str() + "\n";
    stringToSignStream << requestHash;

    auto stringToSignString = stringToSignStream.str();

    auto signatureData = utils::rsaSign(reinterpret_cast<const uint8_t*>(privateRSA.data()), privateRSA.size(), reinterpret_cast<uint8_t*>(stringToSignString.data()), stringToSignString.size());
    auto signature = utils::hexEncode(reinterpret_cast<const uint8_t*>(signatureData.first.get()), signatureData.second);

    string signedUrl = (request.path.empty() ? "/" : request.path) + "?" + query.str() + "&x-goog-signature=" + signature;
    return signedUrl;
}
//---------------------------------------------------------------------------
}; // namespace cloud
}; // namespace anyblob
