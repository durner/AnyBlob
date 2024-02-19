#include "cloud/aws_signer.hpp"
#include "utils/utils.hpp"
#include <algorithm>
#include <map>
#include <sstream>
#include <openssl/evp.h>
#include <openssl/md5.h>
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
void AWSSigner::encodeCanonicalRequest(network::HttpRequest& request, StringToSign& stringToSign, const uint8_t* bodyData, uint64_t bodyLength)
// Creates the canonical request (task 1)
// https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
{
    stringstream requestStream;
    // Step 1, canonicalize request method
    requestStream << network::HttpRequest::getRequestMethod(request.method) << "\n";

    // Step 2, canonicalize request path; assume that path is RFC 3986 conform
    if (request.path.empty())
        requestStream << "/\n";
    else
        requestStream << request.path << "\n";

    // Step 3, canonicalize query; assume that all query arguments are RFC 3986 conform
    if (request.queries.size()) {
        auto it = request.queries.begin();
        while (it != request.queries.end()) {
            requestStream << utils::encodeUrlParameters(it->first) << "=" << utils::encodeUrlParameters(it->second);
            if (++it != request.queries.end())
                requestStream << "&";
        }
    }
    requestStream << "\n";

    if (bodyLength <= (1 << 10)) {
        // Step 6, create sha256 payload string, earlier because of https://docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
        stringToSign.payloadHash = utils::sha256Encode(bodyData, bodyLength);
        request.headers.emplace("x-amz-content-sha256", stringToSign.payloadHash);

        // Step 6a, content-md5 for put
        if ((request.method == network::HttpRequest::Method::PUT || request.method == network::HttpRequest::Method::POST)) {
            auto md5 = utils::md5Encode(bodyData, bodyLength);
            auto encodedResult = utils::base64Encode(reinterpret_cast<unsigned char*>(md5.data()), MD5_DIGEST_LENGTH);
            request.headers.emplace("Content-MD5", encodedResult);
        }
    } else {
        stringToSign.payloadHash = "UNSIGNED-PAYLOAD";
        request.headers.emplace("x-amz-content-sha256", stringToSign.payloadHash);
    }

    // Step 4, canonicalize headers, assume no unnecessary whitespaces in header
    map<string, string> sorted;
    if (request.headers.size()) {
        auto it = request.headers.begin();
        while (it != request.headers.end()) {
            string val = it->first;
            transform(val.begin(), val.end(), val.begin(), [](unsigned char c) { return tolower(c); });
            sorted.emplace(val, it->second);
            ++it;
        }
        for (const auto& h : sorted)
            requestStream << h.first << ":" << h.second << "\n";
    }
    requestStream << "\n";

    // Step 5, create signed headers
    if (sorted.size()) {
        stringstream signedRequests;
        auto it = sorted.begin();
        while (it != sorted.end()) {
            signedRequests << it->first;
            if (++it != sorted.end())
                signedRequests << ";";
        }
        stringToSign.signedHeaders = signedRequests.str();
        requestStream << stringToSign.signedHeaders;
    }
    requestStream << "\n";

    // Step 6 continuing
    requestStream << stringToSign.payloadHash;

    // Step 7, create sha256 request string and return both the request and the sha256 request string
    auto requestString = requestStream.str();
    stringToSign.requestSHA = utils::sha256Encode(reinterpret_cast<uint8_t*>(requestString.data()), requestString.length());
}
//---------------------------------------------------------------------------
string AWSSigner::createStringToSign(const StringToSign& stringToSign)
// Creates the string to sign (task 2)
// https://docs.aws.amazon.com/general/latest/gr/sigv4-create-string-to-sign.html
{
    auto it = stringToSign.request.headers.find("x-amz-date");
    if (it == stringToSign.request.headers.end())
        throw runtime_error("missing x-amz-date");

    stringstream requestStream;
    requestStream << "AWS4-HMAC-SHA256\n";
    requestStream << it->second << "\n";
    requestStream << it->second.substr(0, 8) << "/" << stringToSign.region << "/" << stringToSign.service << "/aws4_request\n";
    requestStream << stringToSign.requestSHA;
    return requestStream.str();
}
//---------------------------------------------------------------------------
string AWSSigner::createSignedRequest(const string& keyId, const string& secret, const StringToSign& stringToSign)
// Calculates the signature for AWS signature version 4 (task 3)
// https://docs.aws.amazon.com/general/latest/gr/sigv4-calculate-signature.html
{
    // Step 1, build derivedSigningKey
    auto it = stringToSign.request.headers.find("x-amz-date");
    if (it == stringToSign.request.headers.end())
        throw runtime_error("missing x-amz-date");

    string kRequest = "aws4_request";
    auto kSecret = "AWS4" + secret;
    auto date = it->second.substr(0, 8);
    auto derivedSigningKey = utils::hmacSign(reinterpret_cast<const uint8_t*>(kSecret.data()), kSecret.length(), reinterpret_cast<const uint8_t*>(date.data()), date.length());
    derivedSigningKey = utils::hmacSign(derivedSigningKey.first.get(), derivedSigningKey.second, reinterpret_cast<const uint8_t*>(stringToSign.region.data()), stringToSign.region.length());
    derivedSigningKey = utils::hmacSign(derivedSigningKey.first.get(), derivedSigningKey.second, reinterpret_cast<const uint8_t*>(stringToSign.service.data()), stringToSign.service.length());
    derivedSigningKey = utils::hmacSign(derivedSigningKey.first.get(), derivedSigningKey.second, reinterpret_cast<const uint8_t*>(kRequest.data()), kRequest.length());

    // Step 2, finally sign the stringToSign with the derivedSigningKey
    auto stringToSignString = createStringToSign(stringToSign);
    derivedSigningKey = utils::hmacSign(derivedSigningKey.first.get(), derivedSigningKey.second, reinterpret_cast<const uint8_t*>(stringToSignString.data()), stringToSignString.length());
    const auto signature = utils::hexEncode(derivedSigningKey.first.get(), derivedSigningKey.second);

    // https://docs.aws.amazon.com/general/latest/gr/sigv4-add-signature-to-request.html (task 4)
    stringstream authorization;
    authorization << "AWS4-HMAC-SHA256"
                  << " Credential=" << keyId << "/" << date << "/" << stringToSign.region << "/" << stringToSign.service << "/" << kRequest << ", SignedHeaders=" << stringToSign.signedHeaders << ", Signature=" << signature;

    stringToSign.request.headers.emplace("Authorization", authorization.str());

    stringstream queryStream;
    if (stringToSign.request.queries.size()) {
        auto it = stringToSign.request.queries.begin();
        while (it != stringToSign.request.queries.end()) {
            queryStream << utils::encodeUrlParameters(it->first) << "=" << utils::encodeUrlParameters(it->second);
            if (++it != stringToSign.request.queries.end())
                queryStream << "&";
        }
    }
    return (stringToSign.request.path.empty() ? "/" : stringToSign.request.path) + "?" + queryStream.str();
}
//---------------------------------------------------------------------------
}; // namespace cloud
}; // namespace anyblob
