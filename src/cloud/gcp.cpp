#include "cloud/gcp.hpp"
#include "cloud/http.hpp"
#include "cloud/gcp_signer.hpp"
#include "network/http_helper.hpp"
#include "network/original_message.hpp"
#include "network/cache.hpp"
#include "network/tasked_send_receiver.hpp"
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
// Creates the AMZ timestamp
{
    stringstream s;
    const auto t = chrono::system_clock::to_time_t(chrono::system_clock::now());
    s << put_time(gmtime(&t), "%Y%m%dT%H%M%SZ");
    return s.str();
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> GCP::downloadInstanceInfo(const string& info)
// Builds the info http request
{
    string httpHeader = "GET /computeMetadata/v1/instance/" + info + " HTTP/1.1\r\nHost: ";
    httpHeader += getIAMAddress();
    httpHeader += "\r\nMetadata-Flavor: Google\r\n\r\n";
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
Provider::Instance GCP::getInstanceDetails(network::TaskedSendReceiverHandle& sendReceiverHandle)
// Uses the send receiver to initialize the secret
{
    auto message = downloadInstanceInfo();
    RemoteInfo info;
    info.endpoint = getIAMAddress();
    info.port = getIAMPort();
    HTTP http(info);
    auto originalMsg = make_unique<network::OriginalMessage>(move(message), http);
    assert(sendReceiverHandle.sendSync(originalMsg.get()));
    assert(sendReceiverHandle.processSync());
    auto& content = originalMsg->result.getDataVector();
    unique_ptr<network::HttpHelper::Info> infoPtr;
    auto s = network::HttpHelper::retrieveContent(content.cdata(), content.size(), infoPtr);

    auto machineType = s.substr(s.find("machineTypes/"));
    for (auto& instance : GCPInstance::getInstanceDetails())
        if (!instance.type.compare(machineType))
            return instance;

    return GCPInstance{string(s), 0, 0, 0};
}
//---------------------------------------------------------------------------
string GCP::getInstanceRegion(network::TaskedSendReceiverHandle& sendReceiverHandle)
// Uses the send receiver to initialize the secret
{
    auto message = downloadInstanceInfo("zone");
    RemoteInfo info;
    info.endpoint = getIAMAddress();
    info.port = getIAMPort();
    HTTP http(info);
    auto originalMsg = make_unique<network::OriginalMessage>(move(message), http);
    assert(sendReceiverHandle.sendSync(originalMsg.get()));
    assert(sendReceiverHandle.processSync());
    auto& content = originalMsg->result.getDataVector();
    unique_ptr<network::HttpHelper::Info> infoPtr;
    auto s = network::HttpHelper::retrieveContent(content.cdata(), content.size(), infoPtr);
    auto region = s.substr(s.find("zones/"));
    region = region.substr(0, region.size() - 2);
    return string(region);
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> GCP::getRequest(const string& filePath, const pair<uint64_t, uint64_t>& range) const
// Builds the http request for downloading a blob
{
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::GET;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    request.path = "/" + filePath;
    request.queries.emplace("X-Goog-Date", testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp());

    request.headers.emplace("Host", getAddress());
    if (range.first != range.second) {
        stringstream rangeString;
        rangeString << "bytes=" << range.first << "-" << range.second;
        request.headers.emplace("Range", rangeString.str());
    }

    GCPSigner::StringToSign stringToSign = {.region = _settings.region, .service = "storage", .signedHeaders = ""};
    request.path = GCPSigner::createSignedRequest(_secret->serviceAccountEmail, _secret->privateKey, request, stringToSign);

    string httpHeader = network::HttpRequest::getRequestMethod(request.method);
    httpHeader += " " + request.path + " ";
    httpHeader += network::HttpRequest::getRequestType(request.type);
    httpHeader += "\r\n";
    for (const auto& h : request.headers)
        httpHeader += h.first + ": " + h.second + "\r\n";
    httpHeader += "\r\n";

    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> GCP::putRequestGeneric(const string& filePath, string_view object, uint16_t part, string_view uploadId) const
// Builds the http request for putting objects without the object data itself
{
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::PUT;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    request.path = "/" + filePath;

    // Is it a multipart upload?
    if (part) {
        request.queries.emplace("partNumber", to_string(part));
        request.queries.emplace("uploadId", uploadId);
    }

    auto date = testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp();
    request.queries.emplace("X-Goog-Date", date);

    request.headers.emplace("Host", getAddress());
    request.headers.emplace("Date", date);
    request.headers.emplace("Content-Length", to_string(object.size()));

    GCPSigner::StringToSign stringToSign = {.region = _settings.region, .service = "storage", .signedHeaders = ""};
    request.path = GCPSigner::createSignedRequest(_secret->serviceAccountEmail, _secret->privateKey, request, stringToSign);

    string httpHeader = network::HttpRequest::getRequestMethod(request.method);
    httpHeader += " " + request.path + " ";
    httpHeader += network::HttpRequest::getRequestType(request.type);
    httpHeader += "\r\n";
    for (const auto& h : request.headers)
        httpHeader += h.first + ": " + h.second + "\r\n";
    httpHeader += "\r\n";

    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> GCP::deleteRequestGeneric(const string& filePath, string_view uploadId) const
// Builds the http request for deleting objects
{
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::DELETE;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    request.path = "/" + filePath;

    // Is it a multipart upload?
    if (!uploadId.empty()) {
        request.queries.emplace("uploadId", uploadId);
    }

    auto date = testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp();
    request.queries.emplace("X-Goog-Date", date);
    request.headers.emplace("Host", getAddress());

    GCPSigner::StringToSign stringToSign = {.region = _settings.region, .service = "storage", .signedHeaders = ""};
    request.path = GCPSigner::createSignedRequest(_secret->serviceAccountEmail, _secret->privateKey, request, stringToSign);

    string httpHeader = network::HttpRequest::getRequestMethod(request.method);
    httpHeader += " " + request.path + " ";
    httpHeader += network::HttpRequest::getRequestType(request.type);
    httpHeader += "\r\n";
    for (const auto& h : request.headers)
        httpHeader += h.first + ": " + h.second + "\r\n";
    httpHeader += "\r\n";

    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> GCP::createMultiPartRequest(const string& filePath) const
// Builds the http request for creating multipart upload objects
{
    network::HttpRequest request;
    request.method = network::HttpRequest::Method::POST;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    request.path = "/" + filePath;
    request.queries.emplace("uploads", "");

    auto date = testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp();
    request.queries.emplace("X-Goog-Date", date);
    request.headers.emplace("Host", getAddress());
    request.headers.emplace("Date", date);

    GCPSigner::StringToSign stringToSign = {.region = _settings.region, .service = "storage", .signedHeaders = ""};
    request.path = GCPSigner::createSignedRequest(_secret->serviceAccountEmail, _secret->privateKey, request, stringToSign);

    string httpHeader = network::HttpRequest::getRequestMethod(request.method);
    httpHeader += " " + request.path + " ";
    httpHeader += network::HttpRequest::getRequestType(request.type);
    httpHeader += "\r\n";
    for (const auto& h : request.headers)
        httpHeader += h.first + ": " + h.second + "\r\n";
    httpHeader += "\r\n";

    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> GCP::completeMultiPartRequest(const string& filePath, string_view uploadId, const std::vector<std::string>& etags, string& content) const
// Builds the http request for completing multipart upload objects
{
    content = "<CompleteMultipartUpload>\n";
    for (auto i = 0ull; i < etags.size(); i++) {
        content += "<Part>\n<PartNumber>";
        content += to_string(i + 1);
        content += "</PartNumber>\n<ETag>\"";
        content += etags[i];
        content += "\"</ETag>\n</Part>\n";
    }
    content += "</CompleteMultipartUpload>\n";

    network::HttpRequest request;
    request.method = network::HttpRequest::Method::POST;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    request.path = "/" + filePath;
    request.queries.emplace("uploadId", uploadId);

    auto date = testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp();
    request.queries.emplace("X-Goog-Date", date);
    request.headers.emplace("Host", getAddress());
    request.headers.emplace("Date", date);
    request.headers.emplace("Content-Length", to_string(content.size()));

    GCPSigner::StringToSign stringToSign = {.region = _settings.region, .service = "storage", .signedHeaders = ""};
    request.path = GCPSigner::createSignedRequest(_secret->serviceAccountEmail, _secret->privateKey, request, stringToSign);

    string httpHeaderMessage = network::HttpRequest::getRequestMethod(request.method);
    httpHeaderMessage += " " + request.path + " ";
    httpHeaderMessage += network::HttpRequest::getRequestType(request.type);
    httpHeaderMessage += "\r\n";

    for (const auto& h : request.headers)
        httpHeaderMessage += h.first + ": " + h.second + "\r\n";
    httpHeaderMessage += "\r\n" + content;
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeaderMessage.data()), reinterpret_cast<uint8_t*>(httpHeaderMessage.data() + httpHeaderMessage.size()));
}
//---------------------------------------------------------------------------
uint32_t GCP::getPort() const
// Gets the port of GCP on http
{
    return _settings.port;
}
//---------------------------------------------------------------------------
string GCP::getAddress() const
// Gets the address of GCP
{
    return _settings.bucket + ".storage.googleapis.com";
}
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
