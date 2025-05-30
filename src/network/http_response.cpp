#include "network/http_response.hpp"
#include "utils/data_vector.hpp"
#include <map>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2024
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
HttpResponse HttpResponse::deserialize(string_view data)
// Deserialize the http header
{
    static constexpr string_view strHttp1_0 = "HTTP/1.0";
    static constexpr string_view strHttp1_1 = "HTTP/1.1";
    static constexpr string_view strNewline = "\r\n";
    static constexpr string_view strHeaderSeperator = ": ";

    HttpResponse response;

    string_view line;
    auto firstLine = true;
    while (true) {
        auto pos = data.find(strNewline);
        if (pos == data.npos)
            throw runtime_error("Invalid HttpResponse: Incomplete header!");

        line = data.substr(0, pos);
        data = data.substr(pos + strNewline.size());
        if (line.empty()) {
            if (!firstLine)
                break;
            else
                throw runtime_error("Invalid HttpResponse: Missing first line!");
        }
        if (firstLine) {
            firstLine = false;
            // the http type
            if (line.starts_with(strHttp1_0)) {
                response.type = Type::HTTP_1_0;
            } else if (line.starts_with(strHttp1_1)) {
                response.type = Type::HTTP_1_1;
            } else {
                throw runtime_error("Invalid HttpResponse: Needs to be a HTTP type 1.0 or 1.1!");
            }

            string_view httpType = getResponseType(response.type);
            line = line.substr(httpType.size() + 1);

            // the response type
            response.code = Code::UNKNOWN;
            for (auto code = static_cast<uint8_t>(Code::OK_200); code <= static_cast<uint8_t>(Code::SLOW_DOWN_503); code++) {
                const string_view responseCode = getResponseCode(static_cast<Code>(code));
                if (line.starts_with(responseCode)) {
                    response.code = static_cast<Code>(code);
                }
            }
        } else {
            // headers
            auto keyPos = line.find(strHeaderSeperator);
            string_view key, value = "";
            if (keyPos == line.npos) {
                throw runtime_error("Invalid HttpResponse: Headers need key and value!");
            } else {
                key = line.substr(0, keyPos);
                value = line.substr(keyPos + strHeaderSeperator.size());
            }
            response.headers.emplace(key, value);
        }
    }

    return response;
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
