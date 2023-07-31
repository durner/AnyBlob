#include "cloud/aws.hpp"
#include "cloud/aws_resolver.hpp"
#include "cloud/aws_signer.hpp"
#include "network/http_helper.hpp"
#include "network/original_message.hpp"
#include "network/tasked_send_receiver.hpp"
#include "utils/data_vector.hpp"
#include <chrono>
#include <iomanip>
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
// Creates the AWS timestamp
{
    stringstream s;
    const auto t = chrono::system_clock::to_time_t(chrono::system_clock::now());
    s << put_time(gmtime(&t), "%Y%m%dT%H%M%SZ");
    return s.str();
}
//---------------------------------------------------------------------------
static int64_t convertIAMTimestamp(string awsTimestamp)
// Creates the AWS timestamp
{
    istringstream s(awsTimestamp);
    tm t{};
    s >> get_time(&t, "%Y-%m-%dT%H:%M:%SZ");
    return mktime(&t);
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::downloadInstanceInfo(const string& info)
// Builds the info http request
{
    string protocol = "http://";
    string httpHeader = "GET /latest/meta-data/" + info + " HTTP/1.1\r\nHost: ";
    httpHeader += getIAMAddress();
    httpHeader += "\r\n\r\n";
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
Provider::Instance AWS::getInstanceDetails(network::TaskedSendReceiver& sendReceiver)
// Uses the send receiver to get instance details
{
    if (_type == Provider::CloudService::AWS) {
        auto message = downloadInstanceInfo();
        auto originalMsg = make_unique<network::OriginalMessage>(move(message), getIAMAddress(), getIAMPort());
        sendReceiver.send(originalMsg.get());
        sendReceiver.process();
        auto& content = originalMsg->result.getDataVector();
        unique_ptr<network::HTTPHelper::Info> infoPtr;
        auto s = network::HTTPHelper::retrieveContent(content.cdata(), content.size(), infoPtr);

        for (auto& instance : AWSInstance::getInstanceDetails())
            if (!instance.type.compare(s))
                return instance;

        return AWSInstance{string(s), 0, 0, ""};
    }
    return AWSInstance{"aws", 0, 0, ""};
}
//---------------------------------------------------------------------------
string AWS::getInstanceRegion(network::TaskedSendReceiver& sendReceiver)
// Uses the send receiver to initialize the secret
{
    auto message = downloadInstanceInfo("placement/region");
    auto originalMsg = make_unique<network::OriginalMessage>(move(message), getIAMAddress(), getIAMPort());
    sendReceiver.send(originalMsg.get());
    sendReceiver.process();
    auto& content = originalMsg->result.getDataVector();
    unique_ptr<network::HTTPHelper::Info> infoPtr;
    auto s = network::HTTPHelper::retrieveContent(content.cdata(), content.size(), infoPtr);
    return string(s);
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::downloadIAMUser() const
// Builds the secret http request
{
    string protocol = "http://";
    string httpHeader = "GET /latest/meta-data/iam/security-credentials HTTP/1.1\r\nHost: ";
    httpHeader += getIAMAddress();
    httpHeader += "\r\n\r\n";
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::downloadSecret(string_view content)
// Builds the secret http request
{
    auto pos = content.find("\n");
    string protocol = "http://";
    string httpHeader = "GET /latest/meta-data/iam/security-credentials/";
    if (!content.substr(0, pos).size())
        return nullptr;
    httpHeader += content.substr(0, pos);
    httpHeader += " HTTP/1.1\r\nHost: ";
    httpHeader += getIAMAddress();
    httpHeader += "\r\n\r\n";

    _secret = make_unique<Secret>();
    _secret->iamUser = content.substr(0, pos);

    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
bool AWS::updateSecret(string_view content)
// Update secret
{
    string needle = "\"AccessKeyId\" : \"";
    auto pos = content.find(needle);
    if (pos == content.npos)
        return false;
    pos += needle.length();
    auto end = content.find("\"", pos);
    _secret->keyId = content.substr(pos, end - pos);

    needle = "\"SecretAccessKey\" : \"";
    pos = content.find(needle);
    if (pos == content.npos)
        return false;
    pos += needle.length();
    end = content.find("\"", pos);
    _secret->secret = content.substr(pos, end - pos);

    needle = "\"Token\" : \"";
    pos = content.find(needle);
    if (pos == content.npos)
        return false;
    pos += needle.length();
    end = content.find("\"", pos);
    _secret->sessionToken = content.substr(pos, end - pos);

    needle = "\"Expiration\" : \"";
    pos = content.find(needle);
    if (pos == content.npos)
        return false;
    pos += needle.length();
    end = content.find("\"", pos);
    auto sv = content.substr(pos, end - pos);
    string timestamp(sv.begin(), sv.end());
    _secret->experiation = convertIAMTimestamp(timestamp);
    return true;
}
//---------------------------------------------------------------------------
bool AWS::validKeys() const
// Checks whether keys need to be refresehd
{
    if (!_secret || ((!_secret->sessionToken.empty() && _secret->experiation - 60 < chrono::system_clock::to_time_t(chrono::system_clock::now())) || _secret->secret.empty()))
        return false;
    return true;
}
//---------------------------------------------------------------------------
void AWS::initSecret(network::TaskedSendReceiver& sendReceiver)
// Uses the send receiver to initialize the secret
{
    if (_type == Provider::CloudService::AWS) {
        auto message = downloadIAMUser();
        auto originalMsg = make_unique<network::OriginalMessage>(move(message), getIAMAddress(), getIAMPort());
        sendReceiver.send(originalMsg.get());
        sendReceiver.process();
        auto& content = originalMsg->result.getDataVector();
        unique_ptr<network::HTTPHelper::Info> infoPtr;
        auto s = network::HTTPHelper::retrieveContent(content.cdata(), content.size(), infoPtr);
        message = downloadSecret(s);
        originalMsg = make_unique<network::OriginalMessage>(move(message), getIAMAddress(), getIAMPort());
        sendReceiver.send(originalMsg.get());
        sendReceiver.process();
        auto& secretContent = originalMsg->result.getDataVector();
        infoPtr.reset();
        s = network::HTTPHelper::retrieveContent(secretContent.cdata(), secretContent.size(), infoPtr);
        updateSecret(s);
    }
}
//---------------------------------------------------------------------------
void AWS::initResolver(network::TaskedSendReceiver& sendReceiver)
// Inits the resolver
{
    if (_type == Provider::CloudService::AWS) {
        sendReceiver.addResolver("amazonaws.com", unique_ptr<network::Resolver>(new cloud::AWSResolver(sendReceiver.getConcurrentRequests())));
    }
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::getRequest(const string& filePath, const pair<uint64_t, uint64_t>& range) const
// Builds the http request for downloading a blob
{
    if (!validKeys())
        return nullptr;

    AWSSigner::Request request;
    request.method = "GET";
    request.type = "HTTP/1.1";

    // If an endpoint is defined, we use the path-style request. The default is the usage of virtual hosted-style requests.
    if (_settings.endpoint.empty())
        request.path = "/" + filePath;
    else
        request.path = "/" + _settings.bucket + "/" + filePath;
    request.bodyData = nullptr;
    request.bodyLength = 0;
    request.headers.emplace("Host", getAddress());
    request.headers.emplace("x-amz-date", testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp());
    request.headers.emplace("x-amz-request-payer", "requester");
    if (!_secret->sessionToken.empty())
        request.headers.emplace("x-amz-security-token", _secret->sessionToken);

    if (range.first != range.second) {
        stringstream rangeString;
        rangeString << "bytes=" << range.first << "-" << range.second;
        request.headers.emplace("Range", rangeString.str());
    }
    auto canonical = AWSSigner::createCanonicalRequest(request);

    AWSSigner::StringToSign stringToSign = {.request = request, .requestSHA = canonical.second, .region = _settings.region, .service = "s3"};
    const auto uri = AWSSigner::createSignedRequest(_secret->keyId, _secret->secret, stringToSign);
    auto httpHeader = request.method + " " + uri + " " + request.type + "\r\n";
    for (auto& h : request.headers)
        httpHeader += h.first + ": " + h.second + "\r\n";
    httpHeader += "\r\n";
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::putRequestGeneric(const string& filePath, string_view object, uint16_t part, string_view uploadId) const
// Builds the http request for putting objects without the object data itself
{
    if (!validKeys())
        return nullptr;

    AWSSigner::Request request;
    request.method = "PUT";
    request.type = "HTTP/1.1";
    request.bodyData = reinterpret_cast<const uint8_t*>(object.data());

    // If an endpoint is defined, we use the path-style request. The default is the usage of virtual hosted-style requests.
    if (_settings.endpoint.empty())
        request.path = "/" + filePath;
    else
        request.path = "/" + _settings.bucket + "/" + filePath;

    // Is it a multipart upload?
    if (part) {
        request.queries.emplace("partNumber", to_string(part));
        request.queries.emplace("uploadId", uploadId);
    }

    request.bodyLength = object.size();
    request.headers.emplace("Host", getAddress());
    request.headers.emplace("x-amz-date", testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp());
    request.headers.emplace("Content-Length", to_string(request.bodyLength));
    request.headers.emplace("x-amz-request-payer", "requester");
    if (!_secret->sessionToken.empty())
        request.headers.emplace("x-amz-security-token", _secret->sessionToken);

    auto canonical = AWSSigner::createCanonicalRequest(request);

    AWSSigner::StringToSign stringToSign = {.request = request, .requestSHA = canonical.second, .region = _settings.region, .service = "s3"};
    const auto uri = AWSSigner::createSignedRequest(_secret->keyId, _secret->secret, stringToSign);
    auto httpHeader = request.method + " " + uri + " " + request.type + "\r\n";
    for (auto& h : request.headers)
        httpHeader += h.first + ": " + h.second + "\r\n";
    httpHeader += "\r\n";
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::deleteRequestGeneric(const string& filePath, string_view uploadId) const
// Builds the http request for deleting an objects
{
    if (!validKeys())
        return nullptr;

    AWSSigner::Request request;
    request.method = "DELETE";
    request.type = "HTTP/1.1";

    // If an endpoint is defined, we use the path-style request. The default is the usage of virtual hosted-style requests.
    if (_settings.endpoint.empty())
        request.path = "/" + filePath;
    else
        request.path = "/" + _settings.bucket + "/" + filePath;

    // Is it a multipart upload?
    if (!uploadId.empty()) {
        request.queries.emplace("uploadId", uploadId);
    }

    request.bodyData = nullptr;
    request.bodyLength = 0;
    request.headers.emplace("Host", getAddress());
    request.headers.emplace("x-amz-date", testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp());
    request.headers.emplace("x-amz-request-payer", "requester");
    if (!_secret->sessionToken.empty())
        request.headers.emplace("x-amz-security-token", _secret->sessionToken);

    auto canonical = AWSSigner::createCanonicalRequest(request);

    AWSSigner::StringToSign stringToSign = {.request = request, .requestSHA = canonical.second, .region = _settings.region, .service = "s3"};
    const auto uri = AWSSigner::createSignedRequest(_secret->keyId, _secret->secret, stringToSign);
    auto httpHeader = request.method + " " + uri + " " + request.type + "\r\n";
    for (auto& h : request.headers)
        httpHeader += h.first + ": " + h.second + "\r\n";
    httpHeader += "\r\n";
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::createMultiPartRequest(const string& filePath) const
// Builds the http request for creating multipart upload objects
{
    if (!validKeys())
        return nullptr;

    AWSSigner::Request request;
    request.method = "POST";
    request.type = "HTTP/1.1";

    // If an endpoint is defined, we use the path-style request. The default is the usage of virtual hosted-style requests.
    if (_settings.endpoint.empty())
        request.path = "/" + filePath;
    else
        request.path = "/" + _settings.bucket + "/" + filePath;
    request.queries.emplace("uploads", "");
    request.bodyData = nullptr;
    request.bodyLength = 0;
    request.headers.emplace("Host", getAddress());
    request.headers.emplace("x-amz-date", testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp());
    request.headers.emplace("x-amz-request-payer", "requester");
    if (!_secret->sessionToken.empty())
        request.headers.emplace("x-amz-security-token", _secret->sessionToken);

    auto canonical = AWSSigner::createCanonicalRequest(request);

    AWSSigner::StringToSign stringToSign = {.request = request, .requestSHA = canonical.second, .region = _settings.region, .service = "s3"};
    const auto uri = AWSSigner::createSignedRequest(_secret->keyId, _secret->secret, stringToSign);
    auto httpHeader = request.method + " " + uri + " " + request.type + "\r\n";
    for (auto& h : request.headers)
        httpHeader += h.first + ": " + h.second + "\r\n";
    httpHeader += "\r\n";
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::completeMultiPartRequest(const string& filePath, string_view uploadId, const std::vector<std::string>& etags) const
// Builds the http request for completing multipart upload objects
{
    if (!validKeys())
        return nullptr;

    string content = "<CompleteMultipartUpload>\n";
    for (auto i = 0ull; i < etags.size(); i++) {
        content += "<Part>\n<PartNumber>";
        content += to_string(i+1);
        content += "</PartNumber>\n<ETag>\"";
        content += etags[i];
        content += "\"</ETag>\n</Part>\n";
    }
    content += "</CompleteMultipartUpload>\n";

    AWSSigner::Request request;
    request.method = "POST";
    request.type = "HTTP/1.1";

    // If an endpoint is defined, we use the path-style request. The default is the usage of virtual hosted-style requests.
    if (_settings.endpoint.empty())
        request.path = "/" + filePath;
    else
        request.path = "/" + _settings.bucket + "/" + filePath;

    request.queries.emplace("uploadId", uploadId);
    request.bodyData = reinterpret_cast<const uint8_t*>(content.data());
    request.bodyLength = content.size();
    request.headers.emplace("Host", getAddress());
    request.headers.emplace("x-amz-date", testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp());
    request.headers.emplace("Content-Length", to_string(content.size()));
    request.headers.emplace("x-amz-request-payer", "requester");
    if (!_secret->sessionToken.empty())
        request.headers.emplace("x-amz-security-token", _secret->sessionToken);

    auto canonical = AWSSigner::createCanonicalRequest(request);

    AWSSigner::StringToSign stringToSign = {.request = request, .requestSHA = canonical.second, .region = _settings.region, .service = "s3"};
    const auto uri = AWSSigner::createSignedRequest(_secret->keyId, _secret->secret, stringToSign);
    auto httpHeaderMessage = request.method + " " + uri + " " + request.type + "\r\n";
    for (auto& h : request.headers)
        httpHeaderMessage += h.first + ": " + h.second + "\r\n";
    httpHeaderMessage += "\r\n" + content;
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeaderMessage.data()), reinterpret_cast<uint8_t*>(httpHeaderMessage.data() + httpHeaderMessage.size()));
}
//---------------------------------------------------------------------------
uint32_t AWS::getPort() const
// Gets the port of AWS S3 on http
{
    return _settings.port;
}
//---------------------------------------------------------------------------
string AWS::getAddress() const
// Gets the address of AWS S3
{
    if (!_settings.endpoint.empty())
        return _settings.endpoint;
    return _settings.bucket + ".s3." + _settings.region + ".amazonaws.com";
}
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
