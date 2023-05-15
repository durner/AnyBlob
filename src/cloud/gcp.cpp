#include "cloud/gcp.hpp"
#include "cloud/gcp_signer.hpp"
#include "network/http_helper.hpp"
#include "network/tasked_send_receiver.hpp"
#include "network/throughput_resolver.hpp"
#include "utils/data_vector.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
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
static string buildAMZTimestamp()
/// Creates the AMZ timestamp
{
    stringstream s;
    const auto t = chrono::system_clock::to_time_t(chrono::system_clock::now());
    s << put_time(gmtime(&t), "%Y%m%dT%H%M%SZ");
    return s.str();
}
//---------------------------------------------------------------------------
void GCP::initKey()
/// Inits key if not exists
{
    ifstream ifs(_secret->rsaKeyFile);
    _secret->privateKey = string((istreambuf_iterator<char>(ifs)), (istreambuf_iterator<char>()));
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> GCP::downloadInstanceInfo(const string& info)
/// Builds the info http request
{
    string protocol = "http://";
    string httpHeader = "GET /computeMetadata/v1/instance/" + info + " HTTP/1.1\r\nHost: ";
    httpHeader += getIAMAddress();
    httpHeader += "\r\nMetadata-Flavor: Google\r\n\r\n";
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
Provider::Instance GCP::getInstanceDetails(network::TaskedSendReceiver& sendReceiver)
/// Uses the send receiver to initialize the secret
{
    auto message = downloadInstanceInfo();
    auto originalMsg = make_unique<network::OriginalMessage>(move(message), getIAMAddress(), getIAMPort());
    sendReceiver.send(originalMsg.get());
    sendReceiver.sendReceive();
    auto content = sendReceiver.receive(originalMsg.get());
    unique_ptr<network::HTTPHelper::Info> infoPtr;
    auto s = network::HTTPHelper::retrieveContent(content->cdata(), content->size(), infoPtr);

    auto machineType = s.substr(s.find("machineTypes/"));
    for (auto& instance : GCPInstance::getInstanceDetails())
        if (!instance.type.compare(machineType))
            return instance;

    return GCPInstance{string(s), 0, 0, ""};
}
//---------------------------------------------------------------------------
string GCP::getInstanceRegion(network::TaskedSendReceiver& sendReceiver)
/// Uses the send receiver to initialize the secret
{
    auto message = downloadInstanceInfo("zone");
    auto originalMsg = make_unique<network::OriginalMessage>(move(message), getIAMAddress(), getIAMPort());
    sendReceiver.send(originalMsg.get());
    sendReceiver.sendReceive();
    auto content = sendReceiver.receive(originalMsg.get());
    unique_ptr<network::HTTPHelper::Info> infoPtr;
    auto s = network::HTTPHelper::retrieveContent(content->cdata(), content->size(), infoPtr);
    auto region = s.substr(s.find("zones/"));
    region = region.substr(0, region.size() - 2);
    return string(region);
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> GCP::getRequest(const string& filePath, const pair<uint64_t, uint64_t>& range) const
/// Builds the http request for downloading a blob
{
    GCPSigner::Request request;
    request.method = "GET";
    request.type = "HTTP/1.1";
    request.path = "/" + filePath;
    request.bodyData = nullptr;
    request.bodyLength = 0;
    request.queries.emplace("X-Goog-Date", testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp());

    request.headers.emplace("Host", getAddress());
    if (range.first != range.second) {
        stringstream rangeString;
        rangeString << "bytes=" << range.first << "-" << range.second;
        request.headers.emplace("Range", rangeString.str());
    }

    GCPSigner::StringToSign stringToSign = {.region = _settings.region, .service = "storage"};
    request.path = GCPSigner::createSignedRequest(_secret->serviceAccountEmail, _secret->privateKey, request, stringToSign);

    auto httpHeader = request.method + " " + request.path + " " + request.type + "\r\n";
    for (auto& h : request.headers)
        httpHeader += h.first + ": " + h.second + "\r\n";
    httpHeader += "\r\n";

    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> GCP::putRequest(const string& filePath, const string_view object) const
/// Builds the http request for putting objects without the object data itself
{
    GCPSigner::Request request;
    request.method = "PUT";
    request.type = "HTTP/1.1";
    request.path = "/" + filePath;
    request.bodyData = reinterpret_cast<const uint8_t*>(object.data());
    request.bodyLength = object.size();

    auto date = testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp();
    request.queries.emplace("X-Goog-Date", date);

    request.headers.emplace("Host", getAddress());
    request.headers.emplace("Date", date);
    request.headers.emplace("Content-Length", to_string(request.bodyLength));

    GCPSigner::StringToSign stringToSign = {.region = _settings.region, .service = "storage"};
    request.path = GCPSigner::createSignedRequest(_secret->serviceAccountEmail, _secret->privateKey, request, stringToSign);

    auto httpHeader = request.method + " " + request.path + " " + request.type + "\r\n";
    for (auto& h : request.headers)
        httpHeader += h.first + ": " + h.second + "\r\n";
    httpHeader += "\r\n";

    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
uint32_t GCP::getPort() const
/// Gets the port of GCP on http
{
    return 80;
}
//---------------------------------------------------------------------------
string GCP::getAddress() const
/// Gets the address of GCP
{
    return _settings.bucket + ".storage.googleapis.com";
}
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
