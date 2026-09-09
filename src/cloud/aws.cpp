#include "cloud/aws.hpp"
#include "cloud/aws_cache.hpp"
#include "cloud/http.hpp"
#include "network/http_helper.hpp"
#include "network/original_message.hpp"
#include "network/tasked_send_receiver.hpp"
#include "utils/data_vector.hpp"
#include "utils/utils.hpp"
#include <cassert>
#include <cerrno>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::cloud {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
thread_local shared_ptr<AWS::Secret> AWS::_secret = nullptr;
thread_local shared_ptr<AWS::Secret> AWS::_sessionSecret = nullptr;
thread_local AWS* AWS::_validInstance = nullptr;
//---------------------------------------------------------------------------
static string buildAMZTimestamp()
// Creates the AWS timestamp
{
    stringstream s;
    const auto t = chrono::system_clock::to_time_t(chrono::system_clock::now());
    std::tm timedata{};
    s << put_time(gmtime_r(&t, &timedata), "%Y%m%dT%H%M%SZ");
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
static void appendIMDSToken(string& httpHeader, string_view token)
// Authenticates the request when the instance requires IMDSv2
{
    if (!token.empty()) {
        httpHeader += "\r\nX-aws-ec2-metadata-token: ";
        httpHeader += token;
    }
    httpHeader += "\r\n\r\n";
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::downloadInstanceInfo(const string& info, string_view token)
// Builds the info http request
{
    string httpHeader = "GET /latest/meta-data/" + info + " HTTP/1.1\r\nHost: ";
    httpHeader += getIAMAddress();
    appendIMDSToken(httpHeader, token);
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
namespace {
//---------------------------------------------------------------------------
/// Milliseconds the metadata reachability probe waits for the connection
constexpr int imdsProbeTimeout = 100;
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> downloadIMDSToken(string_view address)
// Builds the IMDSv2 token request
{
    string httpHeader = "PUT /latest/api/token HTTP/1.1\r\nHost: ";
    httpHeader += address;
    httpHeader += "\r\nX-aws-ec2-metadata-token-ttl-seconds: 21600\r\nContent-Length: 0\r\n\r\n";
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
bool imdsReachable(string_view address, uint32_t port)
// Probe once to avoid repeated deadlines when metadata is unreachable
{
    static const bool reachable = [address, port] {
        auto fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (fd < 0)
            return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        inet_pton(AF_INET, string(address).c_str(), &addr.sin_addr);
        auto connected = !connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (!connected && errno == EINPROGRESS) {
            pollfd poller = {.fd = fd, .events = POLLOUT, .revents = 0};
            if (poll(&poller, 1, imdsProbeTimeout) == 1) {
                auto err = 0;
                socklen_t len = sizeof(err);
                connected = !getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) && !err;
            }
        }
        close(fd);
        return connected;
    }();
    return reachable;
}
//---------------------------------------------------------------------------
string imdsFetch(network::TaskedSendReceiverHandle& sendReceiverHandle, unique_ptr<utils::DataVector<uint8_t>> message, string_view address, uint32_t port)
// Runs one synchronous request against the metadata service
{
    if (!message || !imdsReachable(address, port))
        return {};
    Provider::RemoteInfo info;
    info.endpoint = address;
    info.port = port;
    info.provider = Provider::CloudService::HTTP;
    HTTP http(info);
    auto originalMsg = make_unique<network::OriginalMessage>(move(message), http);
    if (!sendReceiverHandle.sendSync(originalMsg.get()) || !sendReceiverHandle.processSync())
        return {};
    auto& content = originalMsg->result.getDataVector();
    unique_ptr<network::HttpHelper::Info> infoPtr;
    auto s = network::HttpHelper::retrieveContent(content.cdata(), content.size(), infoPtr);
    if (infoPtr && !network::HttpResponse::checkSuccess(infoPtr->response.code))
        return {};
    return string(s);
}
//---------------------------------------------------------------------------
string imdsToken(network::TaskedSendReceiverHandle& sendReceiverHandle, string_view address, uint32_t port)
// Fetch an IMDSv2 token
{
    return imdsFetch(sendReceiverHandle, downloadIMDSToken(address), address, port);
}
//---------------------------------------------------------------------------
/// Credential retry limit
constexpr unsigned secretAttempts = 8;
//---------------------------------------------------------------------------
} // namespace
//---------------------------------------------------------------------------
Provider::Instance AWS::getInstanceDetails(network::TaskedSendReceiverHandle& sendReceiverHandle)
// Uses the send receiver to get instance details
{
    if (_type == Provider::CloudService::AWS) {
        auto s = imdsFetch(sendReceiverHandle, downloadInstanceInfo("instance-type", imdsToken(sendReceiverHandle, getIAMAddress(), getIAMPort())), getIAMAddress(), getIAMPort());

        for (auto& instance : AWSInstance::getInstanceDetails())
            if (!instance.type.compare(s))
                return instance;

        return AWSInstance{string(s), 0, 0, 0};
    }
    return AWSInstance{"aws", 0, 0, 0};
}
//---------------------------------------------------------------------------
string AWS::getInstanceRegion(network::TaskedSendReceiverHandle& sendReceiverHandle)
// Uses the send receiver to get the region
{
    return imdsFetch(sendReceiverHandle, downloadInstanceInfo("placement/region", imdsToken(sendReceiverHandle, getIAMAddress(), getIAMPort())), getIAMAddress(), getIAMPort());
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::downloadIAMUser(string_view token) const
// Builds the secret http request
{
    string httpHeader = "GET /latest/meta-data/iam/security-credentials HTTP/1.1\r\nHost: ";
    httpHeader += getIAMAddress();
    appendIMDSToken(httpHeader, token);
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::downloadSecret(string_view content, string& iamUser, string_view token)
// Builds the secret http request
{
    auto pos = content.find('\n');
    string httpHeader = "GET /latest/meta-data/iam/security-credentials/";
    if (!content.substr(0, pos).size())
        return nullptr;
    httpHeader += content.substr(0, pos);
    httpHeader += " HTTP/1.1\r\nHost: ";
    httpHeader += getIAMAddress();
    appendIMDSToken(httpHeader, token);

    iamUser = content.substr(0, pos);
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
bool AWS::updateSecret(string_view content, string_view iamUser)
// Update secret
{
    auto secret = make_shared<Secret>();
    string needle = "\"AccessKeyId\" : \"";
    auto pos = content.find(needle);
    if (pos == content.npos)
        return false;
    pos += needle.length();
    auto end = content.find('\"', pos);
    secret->keyId = content.substr(pos, end - pos);

    needle = "\"SecretAccessKey\" : \"";
    pos = content.find(needle);
    if (pos == content.npos)
        return false;
    pos += needle.length();
    end = content.find('\"', pos);
    secret->secret = content.substr(pos, end - pos);

    needle = "\"Token\" : \"";
    pos = content.find(needle);
    if (pos == content.npos)
        return false;
    pos += needle.length();
    end = content.find('\"', pos);
    secret->token = content.substr(pos, end - pos);

    needle = "\"Expiration\" : \"";
    pos = content.find(needle);
    if (pos == content.npos)
        return false;
    pos += needle.length();
    end = content.find('\"', pos);
    auto sv = content.substr(pos, end - pos);
    string timestamp(sv.begin(), sv.end());
    secret->expiration = convertIAMTimestamp(timestamp);
    secret->iamUser = iamUser;
    _globalSecret = secret;
    _secret = secret;
    _validInstance = this;
    return true;
}
//---------------------------------------------------------------------------
bool AWS::updateSessionToken(string_view content)
// Update secret
{
    auto secret = make_shared<Secret>();
    string needle = "<AccessKeyId>";
    auto pos = content.find(needle);
    if (pos == content.npos)
        return false;
    pos += needle.length();
    needle = "</AccessKeyId>";
    auto end = content.find(needle, pos);
    secret->keyId = content.substr(pos, end - pos);

    needle = "<SecretAccessKey>";
    pos = content.find(needle);
    if (pos == content.npos)
        return false;
    pos += needle.length();
    needle = "</SecretAccessKey>";
    end = content.find(needle, pos);
    secret->secret = content.substr(pos, end - pos);

    needle = "<SessionToken>";
    pos = content.find(needle);
    if (pos == content.npos)
        return false;
    pos += needle.length();
    needle = "</SessionToken>";
    end = content.find(needle, pos);
    secret->token = content.substr(pos, end - pos);

    needle = "<Expiration>";
    pos = content.find(needle);
    if (pos == content.npos)
        return false;
    pos += needle.length();
    needle = "</Expiration>";
    end = content.find(needle, pos);

    auto sv = content.substr(pos, end - pos);
    string timestamp(sv.begin(), sv.end());
    secret->expiration = convertIAMTimestamp(timestamp);
    _globalSessionSecret = secret;
    _sessionSecret = secret;
    return true;
}
//---------------------------------------------------------------------------
bool AWS::validKeys(uint32_t offset) const
// Checks whether keys need to be refresehd
{
    if (!_secret || _validInstance != this || ((!_secret->token.empty() && _secret->expiration - offset < chrono::system_clock::to_time_t(chrono::system_clock::now())) || _secret->secret.empty()))
        return false;
    return true;
}
//---------------------------------------------------------------------------
bool AWS::validSession(uint32_t offset) const
// Checks whether the session token needs to be refresehd
{
    if (!_sessionSecret || _validInstance != this || ((!_sessionSecret->token.empty() && _sessionSecret->expiration - offset < chrono::system_clock::to_time_t(chrono::system_clock::now())) || _sessionSecret->secret.empty()))
        return false;
    return true;
}
//---------------------------------------------------------------------------
void AWS::initSecret(network::TaskedSendReceiverHandle& sendReceiverHandle)
// Uses the send receiver to initialize the secret
{
    if (_type == Provider::CloudService::AWS && !validKeys(180)) {
        // Bound retries when metadata cannot supply credentials
        for (auto attempt = 0u; attempt < secretAttempts; attempt++) {
            if (_mutex.try_lock()) {
                _secret = _globalSecret;
                _validInstance = this;
                if (validKeys(180)) {
                    _mutex.unlock();
                    return;
                }
                auto token = imdsToken(sendReceiverHandle, getIAMAddress(), getIAMPort());
                auto s = imdsFetch(sendReceiverHandle, downloadIAMUser(token), getIAMAddress(), getIAMPort());
                string iamUser;
                s = imdsFetch(sendReceiverHandle, downloadSecret(s, iamUser, token), getIAMAddress(), getIAMPort());
                updateSecret(s, iamUser);
                _mutex.unlock();
            }
            if (validKeys(60))
                return;
        }
    }
    if (_type == Provider::CloudService::AWS && _settings.zonal && !validSession(180)) {
        for (auto attempt = 0u; attempt < secretAttempts; attempt++) {
            if (_mutex.try_lock()) {
                _sessionSecret = _globalSessionSecret;
                _validInstance = this;
                if (validKeys(180)) {
                    _mutex.unlock();
                    return;
                }
                auto message = getSessionToken();
                RemoteInfo info;
                info.endpoint = _settings.bucket + ".s3.amazonaws.com";
                info.port = getPort();
                info.provider = CloudService::HTTP;
                HTTP http(info);
                auto originalMsg = make_unique<network::OriginalMessage>(move(message), http);
                verify(sendReceiverHandle.sendSync(originalMsg.get()));
                verify(sendReceiverHandle.processSync());
                auto& secretContent = originalMsg->result.getDataVector();
                unique_ptr<network::HttpHelper::Info> infoPtr;
                auto s = network::HttpHelper::retrieveContent(secretContent.cdata(), secretContent.size(), infoPtr);
                updateSessionToken(s);
                _mutex.unlock();
            }
            if (validSession(60))
                return;
        }
    }
}
//---------------------------------------------------------------------------
void AWS::getSecret()
// Updates the local secret
{
    if (!_secret || _validInstance != this) {
        unique_lock lock(_mutex);
        _secret = _globalSecret;
        _validInstance = this;
    }
    if (_type == Provider::CloudService::AWS && _settings.zonal && (!_sessionSecret || _validInstance != this)) {
        unique_lock lock(_mutex);
        _sessionSecret = _globalSessionSecret;
        _validInstance = this;
    }
}
//---------------------------------------------------------------------------
void AWS::initCache(network::TaskedSendReceiverHandle& sendReceiverHandle)
// Inits the cache
{
    assert(sendReceiverHandle.get());
    if (_type == Provider::CloudService::AWS) {
        sendReceiverHandle.get()->addCache("amazonaws.com", unique_ptr<network::Cache>(new cloud::AWSCache()));
    }
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::resignRequest(const utils::DataVector<uint8_t>& data, const uint8_t* bodyData, uint64_t bodyLength) const
// Resigns the request
{
    if (!validKeys() || (_settings.zonal && !validSession()))
        return nullptr;

    auto header = network::HttpRequest::deserialize(string_view(reinterpret_cast<const char*>(data.cdata()), data.size()));
    for (auto it = header.headers.begin(); it != header.headers.end();) {
        if (it->first != "Range" && it->first != "Content-Length")
            it = header.headers.erase(it);
        else
            it++;
    }
    return buildRequest(header, bodyData, bodyLength);
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::buildRequest(network::HttpRequest& request, const uint8_t* bodyData, uint64_t bodyLength, bool initHeaders) const
// Creates and signs the request
{
    shared_ptr<Secret> secret;
    if (initHeaders) {
        request.headers.emplace("Host", getAddress());
        request.headers.emplace("x-amz-date", testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp());
        if (!_settings.zonal) {
            request.headers.emplace("x-amz-request-payer", "requester");
            secret = _secret;
            if (!secret->token.empty())
                request.headers.emplace("x-amz-security-token", secret->token);
        } else {
            secret = _sessionSecret;
            request.headers.emplace("x-amz-s3session-token", secret->token);
        }
    }

    AWSSigner::StringToSign stringToSign = {.request = request, .region = _settings.region, .service = "s3", .requestSHA = "", .signedHeaders = "", .payloadHash = ""};
    AWSSigner::encodeCanonicalRequest(request, stringToSign, bodyData, bodyLength);
    string httpHeader = network::HttpRequest::getRequestMethod(request.method);
    httpHeader += " ";
    httpHeader += AWSSigner::createSignedRequest(secret->keyId, secret->secret, stringToSign) + " " + network::HttpRequest::getRequestType(request.type) + "\r\n";
    for (const auto& h : request.headers)
        httpHeader += h.first + ": " + h.second + "\r\n";
    httpHeader += "\r\n";
    return make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::getRequest(const string& filePath, const pair<uint64_t, uint64_t>& range) const
// Builds the http request for downloading a blob
{
    if (!validKeys() || (_settings.zonal && !validSession()))
        return nullptr;

    network::HttpRequest request;
    request.method = network::HttpRequest::Method::GET;
    request.type = network::HttpRequest::Type::HTTP_1_1;

    // If an endpoint is defined, we use the path-style request. The default is the usage of virtual hosted-style requests.
    if (_settings.endpoint.empty())
        request.path = "/" + utils::encodeUrlPath(filePath);
    else
        request.path = "/" + _settings.bucket + "/" + utils::encodeUrlPath(filePath);

    if (range.first != range.second) {
        assert(range.second > range.first);
        stringstream rangeString;
        rangeString << "bytes=" << range.first << "-" << (range.second - 1);
        request.headers.emplace("Range", rangeString.str());
    }

    return buildRequest(request);
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::putRequestGeneric(const string& filePath, string_view object, uint16_t part, string_view uploadId) const
// Builds the http request for putting objects without the object data itself
{
    if (!validKeys() || (_settings.zonal && !validSession()))
        return nullptr;

    network::HttpRequest request;
    request.method = network::HttpRequest::Method::PUT;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    auto bodyData = reinterpret_cast<const uint8_t*>(object.data());

    // If an endpoint is defined, we use the path-style request. The default is the usage of virtual hosted-style requests.
    if (_settings.endpoint.empty())
        request.path = "/" + utils::encodeUrlPath(filePath);
    else
        request.path = "/" + _settings.bucket + "/" + utils::encodeUrlPath(filePath);

    // Is it a multipart upload?
    if (part) {
        request.queries.emplace("partNumber", to_string(part));
        request.queries.emplace("uploadId", uploadId);
    }

    auto bodyLength = object.size();
    request.headers.emplace("Content-Length", to_string(bodyLength));

    return buildRequest(request, bodyData, bodyLength);
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::deleteRequestGeneric(const string& filePath, string_view uploadId) const
// Builds the http request for deleting an objects
{
    if (!validKeys() || (_settings.zonal && !validSession()))
        return nullptr;

    network::HttpRequest request;
    request.method = network::HttpRequest::Method::DELETE;
    request.type = network::HttpRequest::Type::HTTP_1_1;

    // If an endpoint is defined, we use the path-style request. The default is the usage of virtual hosted-style requests.
    if (_settings.endpoint.empty())
        request.path = "/" + utils::encodeUrlPath(filePath);
    else
        request.path = "/" + _settings.bucket + "/" + utils::encodeUrlPath(filePath);

    // Is it a multipart upload?
    if (!uploadId.empty()) {
        request.queries.emplace("uploadId", uploadId);
    }

    return buildRequest(request);
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::createMultiPartRequest(const string& filePath) const
// Builds the http request for creating multipart upload objects
{
    if (!validKeys() || (_settings.zonal && !validSession()))
        return nullptr;

    network::HttpRequest request;
    request.method = network::HttpRequest::Method::POST;
    request.type = network::HttpRequest::Type::HTTP_1_1;

    // If an endpoint is defined, we use the path-style request. The default is the usage of virtual hosted-style requests.
    if (_settings.endpoint.empty())
        request.path = "/" + utils::encodeUrlPath(filePath);
    else
        request.path = "/" + _settings.bucket + "/" + utils::encodeUrlPath(filePath);
    request.queries.emplace("uploads", "");

    return buildRequest(request);
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::completeMultiPartRequest(const string& filePath, string_view uploadId, const std::vector<std::string>& etags, string& content) const
// Builds the http request for completing multipart upload objects
{
    if (!validKeys() || (_settings.zonal && !validSession()))
        return nullptr;

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

    // If an endpoint is defined, we use the path-style request. The default is the usage of virtual hosted-style requests.
    if (_settings.endpoint.empty())
        request.path = "/" + utils::encodeUrlPath(filePath);
    else
        request.path = "/" + _settings.bucket + "/" + utils::encodeUrlPath(filePath);

    request.queries.emplace("uploadId", uploadId);
    auto bodyData = reinterpret_cast<const uint8_t*>(content.data());
    auto bodyLength = content.size();
    request.headers.emplace("Content-Length", to_string(bodyLength));

    return buildRequest(request, bodyData, bodyLength);
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> AWS::getSessionToken(string_view type) const
// Builds the http request for retrieving the session token of createsession
{
    if (!validKeys())
        return nullptr;

    network::HttpRequest request;
    request.method = network::HttpRequest::Method::GET;
    request.type = network::HttpRequest::Type::HTTP_1_1;
    request.path = "/";
    request.queries.emplace("session", "");
    request.headers.emplace("Host", _settings.bucket + ".s3.amazonaws.com");
    request.headers.emplace("x-amz-create-session-mode", type);
    request.headers.emplace("x-amz-date", testEnviornment ? fakeAMZTimestamp : buildAMZTimestamp());
    if (!_secret->token.empty())
        request.headers.emplace("x-amz-security-token", _secret->token);

    return buildRequest(request, nullptr, 0, false);
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
    if (_settings.zonal) {
        // remove --x-s3 and use at most 9 characters az id + --
        auto bucket = _settings.bucket.substr(0, _settings.bucket.size() - 6);
        bucket = bucket.substr(bucket.size() - 11);
        auto find = bucket.find("--");
        auto zone = bucket.substr(find + 2);
        return _settings.bucket + ".s3express-" + zone + "." + _settings.region + ".amazonaws.com";
    }
    return _settings.bucket + ".s3." + _settings.region + ".amazonaws.com";
}
//---------------------------------------------------------------------------
} // namespace anyblob::cloud
