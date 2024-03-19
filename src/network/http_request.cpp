#include "network/http_request.hpp"
#include "utils/data_vector.hpp"
#include "utils/utils.hpp"
#include <iostream>
#include <map>
#include <sstream>
#include <string>
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
HttpRequest HttpRequest::deserialize(string_view data)
// Deserialize the http header
{
    static constexpr string_view strHttp1_0 = "HTTP/1.0";
    static constexpr string_view strHttp1_1 = "HTTP/1.1";
    static constexpr string_view strGet = "GET";
    static constexpr string_view strPost = "POST";
    static constexpr string_view strPut = "PUT";
    static constexpr string_view strDelete = "DELETE";
    static constexpr string_view strNewline = "\r\n";
    static constexpr string_view strHeaderSeperator = ": ";
    static constexpr string_view strQuerySeperator = "=";
    static constexpr string_view strQueryStart = "?";
    static constexpr string_view strQueryAnd = "&";

    HttpRequest request;

    string_view line;
    auto firstLine = true;
    while (true) {
        auto pos = data.find(strNewline);
        if (pos == data.npos)
            throw runtime_error("Invalid HttpRequest: Incomplete header!");

        line = data.substr(0, pos);
        data = data.substr(pos + strNewline.size());
        if (line.empty()) {
            if (!firstLine)
                break;
            else
                throw runtime_error("Invalid HttpRequest: Missing first line!");
        }
        if (firstLine) {
            firstLine = false;
            // parse method
            if (line.starts_with(strGet)) {
                request.method = Method::GET;
                line = line.substr(strGet.size());
            } else if (line.starts_with(strPost)) {
                request.method = Method::POST;
                line = line.substr(strPost.size());
            } else if (line.starts_with(strPut)) {
                request.method = Method::PUT;
                line = line.substr(strPut.size());
            } else if (line.starts_with(strDelete)) {
                request.method = Method::DELETE;
                line = line.substr(strDelete.size());
            } else {
                throw runtime_error("Invalid HttpRequest: Needs to start with request method!");
            }

            // parse path, requires HTTP type, otherwise invalid
            pos = line.find(" ", 1);
            if (pos == line.npos)
                throw runtime_error("Invalid HttpRequest: Could not find path, or missing HTTP type!");
            auto pathQuery = line.substr(1, pos - 1);
            // the http type
            line = line.substr(pos + 1);

            // split path and query
            auto queriesPos = pathQuery.find(strQueryStart);
            if (queriesPos != pathQuery.npos) {
                // with query
                request.path = pathQuery.substr(0, queriesPos);
                auto queries = pathQuery.substr(queriesPos + 1);
                while (true) {
                    auto queryPos = queries.find(strQueryAnd);
                    string_view query;
                    if (queryPos == queries.npos)
                        query = queries;
                    else
                        query = queries.substr(0, queryPos);

                    // split between key and value (value might be unnecassary)
                    auto keyPos = query.find(strQuerySeperator);
                    string_view key, value = "";
                    if (keyPos == query.npos) {
                        key = query;
                    } else {
                        key = query.substr(0, keyPos);
                        value = query.substr(keyPos + 1);
                    }
                    if (key.size() > 0)
                        request.queries.emplace(key, value);
                    if (queryPos == queries.npos)
                        break;
                    queries = queries.substr(queryPos + 1);
                }
            } else {
                request.path = pathQuery;
            }

            // the http type
            if (line.starts_with(strHttp1_0)) {
                request.type = Type::HTTP_1_0;
            } else if (line.starts_with(strHttp1_1)) {
                request.type = Type::HTTP_1_1;
            } else {
                throw runtime_error("Invalid HttpRequest: Needs to be a HTTP type 1.0 or 1.1!");
            }
        } else {
            // headers
            auto keyPos = line.find(strHeaderSeperator);
            string_view key, value = "";
            if (keyPos == line.npos) {
                throw runtime_error("Invalid HttpRequest: Headers need key and value!");
            } else {
                key = line.substr(0, keyPos);
                value = line.substr(keyPos + strHeaderSeperator.size());
            }
            request.headers.emplace(key, value);
        }
    }

    return request;
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> HttpRequest::serialize(const HttpRequest& request)
// Serialize an http header
{
    string httpHeader = getRequestMethod(request.method);
    httpHeader += " " + request.path;
    if (request.queries.size())
         httpHeader += "?";
    auto it = request.queries.begin();
    while (it != request.queries.end()) {
        httpHeader += utils::encodeUrlParameters(it->first) + "=" + utils::encodeUrlParameters(it->second);
        if (++it != request.queries.end())
            httpHeader += "&";
    }
    httpHeader += " ";
    httpHeader += getRequestType(request.type);
    httpHeader += "\r\n";
    for (const auto& h : request.headers)
        httpHeader += h.first + ": " + h.second + "\r\n";
    httpHeader += "\r\n";
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
