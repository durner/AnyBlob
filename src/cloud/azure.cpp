#include "cloud/azure.hpp"
#include "cloud/azure_signer.hpp"
#include "network/http_helper.hpp"
#include "network/original_message.hpp"
#include "network/tasked_send_receiver.hpp"
#include "network/throughput_resolver.hpp"
#include "utils/data_vector.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
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
static string buildXMSTimestamp()
// Creates the X-MS timestamp
{
    stringstream s;
    const auto t = chrono::system_clock::to_time_t(chrono::system_clock::now());
    s << put_time(gmtime(&t), "%a, %d %b %Y %H:%M:%S GMT");
    return s.str();
}
//---------------------------------------------------------------------------
void Azure::initKey()
// Inits key if not exists
{
    _secret->privateKey.erase(remove(_secret->privateKey.begin(), _secret->privateKey.end(), '\n'), _secret->privateKey.cend());
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> Azure::downloadInstanceInfo()
// Builds the info http request
{
    string httpHeader = "GET /metadata/instance?api-version=2021-02-01 HTTP/1.1\r\nHost: ";
    httpHeader += getIAMAddress();
    httpHeader += "\r\nMetadata: true\r\n\r\n";
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
Provider::Instance Azure::getInstanceDetails(network::TaskedSendReceiver& sendReceiver)
// Uses the send receiver to initialize the secret
{
    auto message = downloadInstanceInfo();
    auto originalMsg = make_unique<network::OriginalMessage>(move(message), getIAMAddress(), getIAMPort());
    sendReceiver.send(originalMsg.get());
    sendReceiver.process();
    auto& content = originalMsg->result.getDataVector();
    unique_ptr<network::HTTPHelper::Info> infoPtr;
    auto s = network::HTTPHelper::retrieveContent(content.cdata(), content.size(), infoPtr);

    string needle = "\"vmSize\" : \"";
    auto pos = s.find(needle);
    if (pos != s.npos) {
        pos += needle.length();
        auto end = s.find("\"", pos);
        auto vmType = s.substr(pos, end - pos);

        for (auto& instance : AzureInstance::getInstanceDetails())
            if (!instance.type.compare(vmType))
                return instance;
    }
    return AzureInstance{string(s), 0, 0, ""};
}
//---------------------------------------------------------------------------
string Azure::getRegion(network::TaskedSendReceiver& sendReceiver)
// Uses the send receiver to initialize the secret
{
    auto message = downloadInstanceInfo();
    auto originalMsg = make_unique<network::OriginalMessage>(move(message), getIAMAddress(), getIAMPort());
    sendReceiver.send(originalMsg.get());
    sendReceiver.process();
    auto& content = originalMsg->result.getDataVector();
    unique_ptr<network::HTTPHelper::Info> infoPtr;
    auto s = network::HTTPHelper::retrieveContent(content.cdata(), content.size(), infoPtr);

    string needle = "\"location\" : \"";
    auto pos = s.find(needle);
    if (pos == s.npos)
        throw;
    pos += needle.length();
    auto end = s.find("\"", pos);
    auto region = s.substr(pos, end - pos);

    return string(region);
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> Azure::getRequest(const string& filePath, const pair<uint64_t, uint64_t>& range) const
// Builds the http request for downloading a blob
{
    AzureSigner::Request request;
    request.method = "GET";
    request.type = "HTTP/1.1";
    request.path = "/" + _settings.container + "/" + filePath;
    request.bodyData = nullptr;
    request.bodyLength = 0;

    request.headers.emplace("x-ms-date", testEnviornment ? fakeXMSTimestamp : buildXMSTimestamp());
    request.headers.emplace("Host", getAddress());
    if (range.first != range.second) {
        stringstream rangeString;
        rangeString << "bytes=" << range.first << "-" << range.second;
        request.headers.emplace("Range", rangeString.str());
    }

    request.path = AzureSigner::createSignedRequest(_secret->accountName, _secret->privateKey, request);

    auto httpHeader = request.method + " " + request.path + " " + request.type + "\r\n";
    for (auto& h : request.headers)
        httpHeader += h.first + ": " + h.second + "\r\n";
    httpHeader += "\r\n";

    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> Azure::putRequest(const string& filePath, const string_view object) const
// Builds the http request for putting objects without the object data itself
{
    AzureSigner::Request request;
    request.method = "PUT";
    request.type = "HTTP/1.1";
    request.path = "/" + _settings.container + "/" + filePath;
    request.bodyData = reinterpret_cast<const uint8_t*>(object.data());
    request.bodyLength = object.size();

    auto date = testEnviornment ? fakeXMSTimestamp : buildXMSTimestamp();
    request.headers.emplace("x-ms-date", date);
    request.headers.emplace("x-ms-blob-type", "BlockBlob");
    request.headers.emplace("Host", getAddress());
    request.headers.emplace("Content-Length", to_string(request.bodyLength));

    request.path = AzureSigner::createSignedRequest(_secret->accountName, _secret->privateKey, request);

    auto httpHeader = request.method + " " + request.path + " " + request.type + "\r\n";
    for (auto& h : request.headers)
        httpHeader += h.first + ": " + h.second + "\r\n";
    httpHeader += "\r\n";

    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
uint32_t Azure::getPort() const
// Gets the port of Azure on http
{
    return _settings.port;
}
//---------------------------------------------------------------------------
string Azure::getAddress() const
// Gets the address of Azure
{
    return _secret->accountName + ".blob.core.windows.net";
}
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
