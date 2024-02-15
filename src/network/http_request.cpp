#include "network/http_request.hpp"
#include "utils/data_vector.hpp"
#include "utils/utils.hpp"
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2024
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
HttpRequest HttpRequest::deserialize(string_view /*data*/)
// Deserialize the http header
{
    HttpRequest request;
    return request;
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> HttpRequest::serialize(const HttpRequest& request)
// Serialize an http header
{
    string httpHeader = getRequestMethod(request);
    httpHeader += " ";
    auto it = request.queries.begin();
    while (it != request.queries.end()) {
        httpHeader += utils::encodeUrlParameters(it->first) + "=" + utils::encodeUrlParameters(it->second);
        if (++it != request.queries.end())
            httpHeader += "&";
    }
    httpHeader += " ";
    httpHeader += getRequestType(request);
    httpHeader += "\r\n";
    for (auto& h : request.headers)
        httpHeader += h.first + ": " + h.second + "\r\n";
    httpHeader += "\r\n";
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
