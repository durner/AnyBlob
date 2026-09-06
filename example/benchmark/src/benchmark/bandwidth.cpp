#include "benchmark/bandwidth.hpp"
#include "cloud/aws.hpp"
#include "cloud/aws_cache.hpp"
#include "cloud/azure.hpp"
#include "cloud/gcp.hpp"
#include "network/original_message.hpp"
#include "network/s3_send_receiver.hpp"
#include "network/tasked_send_receiver.hpp"
#include "network/transaction.hpp"
#include "perfevent/PerfEvent.hpp"
#include "utils/timer.hpp"
#include "utils/utils.hpp"
#include <azure/core/credentials/credentials.hpp>
#include <azure/core/exception.hpp>
#include <azure/core/http/policies/policy.hpp>
#include <azure/storage/blobs.hpp>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <latch>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <unistd.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::benchmark {
//---------------------------------------------------------------------------
using namespace std;
using namespace anyblob;
//---------------------------------------------------------------------------
namespace {
constexpr string_view DetailHeader = "Algorithm,Resolver,Iteration,Threads,Concurrency,HTTPS,Encryption,Start,Finish,Diff,ReceiveLatency,Size,RunId";
constexpr string_view AzureSummaryHeader = "Step,Pct,Time,CPUUserTime,CPUSysTime,CPUElapsedTime,CPUActiveAllProcesses,CPUIdleAllProcesses,Algorithm,Resolver,Iteration,Threads,Concurrency,Requests,Datasize,HTTPS,Encryption,Attempts,Retries,MaxConcurrency,RunId";
constexpr string_view UringSummaryHeader = "Step,Pct,Time,CPUUserTime,CPUSysTime,CPUElapsedTime,CPUActiveAllProcesses,CPUIdleAllProcesses,Algorithm,Resolver,Iteration,Threads,Concurrency,Requests,Datasize,HTTPS,Encryption,RunId";
constexpr string_view ManifestHeader = "Schema,RunId,Algorithm,Iteration,LogicalRequests,Attempts,Retries,VerifiedBytes,Failures,ConfiguredConcurrency,MaxConcurrency";

class FixedBearerCredential final : public Azure::Core::Credentials::TokenCredential {
    string token;

    public:
    explicit FixedBearerCredential(string value) : TokenCredential("AnyBlobBenchmarkBearer"), token(move(value)) {}

    Azure::Core::Credentials::AccessToken GetToken(
        const Azure::Core::Credentials::TokenRequestContext&,
        const Azure::Core::Context&) const override {
        return {token, Azure::DateTime::max()};
    }
};

class AttemptCountingPolicy final : public Azure::Core::Http::Policies::HttpPolicy {
    shared_ptr<atomic<uint64_t>> attempts;

    public:
    explicit AttemptCountingPolicy(shared_ptr<atomic<uint64_t>> value) : attempts(move(value)) {}

    unique_ptr<Azure::Core::Http::RawResponse> Send(
        Azure::Core::Http::Request& request,
        Azure::Core::Http::Policies::NextHttpPolicy nextPolicy,
        const Azure::Core::Context& context) const override {
        ++*attempts;
        return nextPolicy.Send(request, context);
    }

    unique_ptr<HttpPolicy> Clone() const override {
        return make_unique<AttemptCountingPolicy>(attempts);
    }
};

void appendFile(const string& path, const string& content) {
    ofstream output(path, ios::app);
    if (!output)
        throw runtime_error("Unable to open benchmark report: " + path);
    output << content;
    if (!output)
        throw runtime_error("Unable to write benchmark report: " + path);
}

void validateCsvSchema(const string& path, string_view header) {
    ifstream existing(path);
    string existingHeader;
    if (existing && getline(existing, existingHeader)) {
        if (existingHeader != header)
            throw runtime_error("Existing benchmark report has an incompatible schema: " + path);
    }
}

void appendCsv(const string& path, string_view header, const string& rows) {
    validateCsvSchema(path, header);
    if (ifstream(path)) {
        appendFile(path, rows);
        return;
    }
    appendFile(path, string(header) + "\n" + rows);
}

void appendRunManifest(
    const string& reportPath,
    const string& runId,
    const string& algorithm,
    uint32_t iteration,
    uint64_t logicalRequests,
    uint64_t attempts,
    uint64_t verifiedBytes,
    uint64_t failures,
    uint64_t configuredConcurrency,
    uint64_t observedConcurrency) {
    const string manifestPath = reportPath + ".manifest";
    ostringstream manifest;
    manifest << "1," << runId << "," << algorithm << "," << iteration << "," << logicalRequests << "," << attempts << ","
             << attempts - logicalRequests << "," << verifiedBytes << "," << failures << ","
             << configuredConcurrency << "," << observedConcurrency << "\n";
    appendCsv(
        manifestPath,
        ManifestHeader,
        manifest.str());
}

string makeRunId(string_view algorithm, uint32_t iteration) {
    const auto now = chrono::duration_cast<chrono::nanoseconds>(
        chrono::system_clock::now().time_since_epoch()).count();
    return string(algorithm) + "-" + to_string(now) + "-" + to_string(getpid()) + "-" + to_string(iteration);
}

void updateMaximum(atomic<uint64_t>& maximum, uint64_t value) {
    auto observed = maximum.load();
    while (observed < value && !maximum.compare_exchange_weak(observed, value)) {
    }
}

bool matchesContentRange(string_view value, uint64_t offset, uint64_t length) {
    const string expected = "bytes " + to_string(offset) + "-" + to_string(offset + length - 1) + "/";
    if (!value.starts_with(expected))
        return false;
    const auto total = value.substr(expected.size());
    return total == "*" || (!total.empty() && all_of(total.begin(), total.end(), [](char c) {
        return c >= '0' && c <= '9';
    }));
}

bool parseUnsigned(string_view value, uint64_t& result) {
    const auto [end, error] = from_chars(value.data(), value.data() + value.size(), result);
    return error == errc() && end == value.data() + value.size();
}
} // namespace
//---------------------------------------------------------------------------
void Bandwidth::run(const Settings& benchmarkSettings, const string& uri)
// The bandwith benchmark
{
    for (auto s : benchmarkSettings.systems) {
        switch (s) {
            case Systems::Uring:
                runUring(benchmarkSettings, uri);
                break;
            case Systems::S3Crt:
                runS3<network::S3CrtSendReceiver>(benchmarkSettings, uri);
                break;
            case Systems::S3:
                runS3<network::S3CurlSendReceiver>(benchmarkSettings, uri);
                break;
            case Systems::AzureSdk:
                runAzureSdk(benchmarkSettings, uri);
                break;
        }
    }
}
//---------------------------------------------------------------------------
void Bandwidth::runAzureSdk(const Settings& benchmarkSettings, const string& uri)
{
    const char* token = getenv("AZURE_STORAGE_BEARER_TOKEN");
    if (!token || !*token)
        throw runtime_error("AZURE_STORAGE_BEARER_TOKEN is not set");
    if (string_view(token).find_first_of("\r\n") != string_view::npos)
        throw runtime_error("AZURE_STORAGE_BEARER_TOKEN contains invalid characters");

    const auto remoteInfo = cloud::Provider::getRemoteInfo(uri);
    const string blobUrl = "https://onelake.blob.fabric.microsoft.com/" + remoteInfo.bucket + "/" + benchmarkSettings.objectPath;
    auto credential = make_shared<FixedBearerCredential>(token);

    for (uint32_t iteration = 0; iteration < benchmarkSettings.iterations; ++iteration) {
        const auto runId = makeRunId("azuresdk", iteration);
        if (!benchmarkSettings.report.empty()) {
            validateCsvSchema(benchmarkSettings.report, DetailHeader);
            validateCsvSchema(benchmarkSettings.report + ".summary", AzureSummaryHeader);
            validateCsvSchema(benchmarkSettings.report + ".manifest", ManifestHeader);
        }
        auto attempts = make_shared<atomic<uint64_t>>(0);
        Azure::Storage::Blobs::BlobClientOptions clientOptions;
        clientOptions.ApiVersion = "2021-06-08";
        clientOptions.Retry.MaxRetries = 0;
        clientOptions.PerRetryPolicies.emplace_back(make_unique<AttemptCountingPolicy>(attempts));
        clientOptions.Transport.Transport = benchmarkSettings.azureTransport;
        auto client = make_shared<Azure::Storage::Blobs::BlobClient>(blobUrl, credential, clientOptions);

        vector<utils::TimingHelper> timings(benchmarkSettings.requests);
        atomic<uint64_t> nextRequest{0};
        atomic<uint64_t> finished{0};
        atomic<uint64_t> failed{0};
        atomic<uint64_t> verifiedBytes{0};
        atomic<uint64_t> inFlight{0};
        atomic<uint64_t> observedConcurrency{0};
        atomic<bool> abort{false};
        Azure::Core::Context parentContext;
        mutex errorMutex;
        string firstError;

        const uint64_t configuredInFlight
            = static_cast<uint64_t>(benchmarkSettings.concurrentThreads) * benchmarkSettings.concurrentRequests;
        const uint64_t workerCount = min(benchmarkSettings.requests, configuredInFlight);
        vector<vector<uint8_t>> buffers;
        buffers.reserve(workerCount);
        for (uint64_t worker = 0; worker < workerCount; ++worker)
            buffers.emplace_back(benchmarkSettings.readLength);
        latch workersReady(workerCount);
        latch startGate(1);
        latch workersDone(workerCount);
        vector<jthread> workers;
        workers.reserve(workerCount);

        try {
            for (uint64_t worker = 0; worker < workerCount; ++worker) {
                workers.emplace_back([&, worker] {
                    workersReady.count_down();
                    startGate.wait();
                    while (!abort) {
                    const auto requestId = nextRequest.fetch_add(1);
                    if (requestId >= benchmarkSettings.requests)
                        break;

                    auto& timing = timings[requestId];
                    timing.start = chrono::steady_clock::now();
                    const auto active = inFlight.fetch_add(1) + 1;
                    updateMaximum(observedConcurrency, active);
                    try {
                        Azure::Storage::Blobs::DownloadBlobOptions downloadOptions;
                        Azure::Core::Http::HttpRange requestedRange;
                        requestedRange.Offset = static_cast<int64_t>(benchmarkSettings.readOffset);
                        requestedRange.Length = static_cast<int64_t>(benchmarkSettings.readLength);
                        downloadOptions.Range = requestedRange;
                        const auto deadline = Azure::DateTime(
                            chrono::system_clock::now() + chrono::milliseconds(benchmarkSettings.requestTimeoutMs));
                        auto context = parentContext.WithDeadline(deadline);
                        auto response = client->Download(downloadOptions, context);

                        const auto& headers = response.RawResponse->GetHeaders();
                        const auto contentLength = headers.find("content-length");
                        uint64_t parsedContentLength = 0;
                        if (response.RawResponse->GetStatusCode() != Azure::Core::Http::HttpStatusCode::PartialContent
                            || response.Value.ContentRange.Offset != static_cast<int64_t>(benchmarkSettings.readOffset)
                            || !response.Value.ContentRange.Length.HasValue()
                            || response.Value.ContentRange.Length.Value() != static_cast<int64_t>(benchmarkSettings.readLength)
                            || contentLength == headers.end()
                            || !parseUnsigned(contentLength->second, parsedContentLength)
                            || parsedContentLength != benchmarkSettings.readLength) {
                            throw logic_error("response_validation");
                        }

                        auto& body = buffers[worker];
                        const auto bytes = response.Value.BodyStream->ReadToCount(body.data(), body.size(), context);
                        uint8_t extra;
                        const auto trailing = response.Value.BodyStream->Read(&extra, 1, context);
                        if (bytes != benchmarkSettings.readLength || trailing != 0)
                            throw logic_error("response_validation");

                        timing.size = bytes;
                        verifiedBytes += bytes;
                    } catch (const Azure::Core::RequestFailedException&) {
                        abort = true;
                        parentContext.Cancel();
                        ++failed;
                        lock_guard<mutex> lock(errorMutex);
                        if (firstError.empty())
                            firstError = "azure_request_failed";
                    } catch (const Azure::Core::OperationCancelledException&) {
                        abort = true;
                        parentContext.Cancel();
                        ++failed;
                        lock_guard<mutex> lock(errorMutex);
                        if (firstError.empty())
                            firstError = "request_cancelled";
                    } catch (const logic_error&) {
                        abort = true;
                        parentContext.Cancel();
                        ++failed;
                        lock_guard<mutex> lock(errorMutex);
                        if (firstError.empty())
                            firstError = "response_validation";
                    } catch (...) {
                        abort = true;
                        parentContext.Cancel();
                        ++failed;
                        lock_guard<mutex> lock(errorMutex);
                        if (firstError.empty())
                            firstError = "unexpected_failure";
                    }
                    timing.finish = chrono::steady_clock::now();
                    timing.recieve = timing.finish;
                    --inFlight;
                    ++finished;
                }
                    workersDone.count_down();
                });
            }
        } catch (...) {
            abort = true;
            startGate.count_down();
            throw;
        }
        workersReady.wait();
        const auto runStart = chrono::steady_clock::now();
        startGate.count_down();
        workersDone.wait();
        const auto runFinish = chrono::steady_clock::now();
        workers.clear();

        if (failed || finished != benchmarkSettings.requests
            || verifiedBytes != benchmarkSettings.requests * benchmarkSettings.readLength) {
            throw runtime_error(
                "Azure SDK read validation failed: " + (firstError.empty() ? string("unknown error") : firstError));
        }

        const auto observedAttempts = attempts->load();
        const auto retries = observedAttempts - benchmarkSettings.requests;
        cout << "OneLake AzureSdk completed: requests=" << finished << " attempts=" << observedAttempts
             << " retries=" << retries << " bytes=" << verifiedBytes << " failures=" << failed
             << " max_concurrency=" << observedConcurrency << endl;

        if (!benchmarkSettings.report.empty()) {
            ostringstream report;
            for (const auto& timing : timings) {
                report << "AzureSdk,," << iteration << "," << benchmarkSettings.concurrentThreads << ","
                       << benchmarkSettings.concurrentRequests << ",1,0," << systemClockToMys(timing.start) << ","
                       << systemClockToMys(timing.finish) << ","
                       << chrono::duration_cast<chrono::microseconds>(timing.finish - timing.start).count()
                       << ",0," << timing.size << "," << runId << "\n";
            }
            appendCsv(
                benchmarkSettings.report,
                DetailHeader,
                report.str());

            const string summaryPath = benchmarkSettings.report + ".summary";
            ostringstream summary;
            summary << "Download,1,"
                    << static_cast<double>(chrono::duration_cast<chrono::nanoseconds>(runFinish - runStart).count()) / 1000000.0
                    << ",-1,-1,-1,-1,-1,AzureSdk,," << iteration << "," << benchmarkSettings.concurrentThreads << ","
                    << benchmarkSettings.concurrentRequests << "," << benchmarkSettings.requests << ","
                    << verifiedBytes << ",1,0," << observedAttempts << "," << retries << ","
                    << observedConcurrency << "," << runId << "\n";
            appendCsv(
                summaryPath,
                AzureSummaryHeader,
                summary.str());
            appendRunManifest(
                benchmarkSettings.report,
                runId,
                "AzureSdk",
                iteration,
                benchmarkSettings.requests,
                observedAttempts,
                verifiedBytes,
                failed,
                configuredInFlight,
                observedConcurrency);
        }
    }
}
//---------------------------------------------------------------------------
template <typename S3SendReceiver>
void Bandwidth::runS3(const Settings& benchmarkSettings, const string& uri)
// The bandwith benchmark for S3 interface
{
    auto remoteInfo = anyblob::cloud::Provider::getRemoteInfo(uri);
    for (auto iteration = 0u; iteration < benchmarkSettings.iterations; iteration++) {
        unique_ptr<S3SendReceiver> sendReceiver = make_unique<S3SendReceiver>(benchmarkSettings.requests << 1, benchmarkSettings.concurrentRequests, remoteInfo.region, benchmarkSettings.concurrentThreads, benchmarkSettings.https);

        for (uint64_t i = 0; i < benchmarkSettings.concurrentRequests << 1; i++) {
            sendReceiver->reuse(make_unique<utils::DataVector<uint8_t>>(64 << 20));
        }
        future<void> asyncSendReceiverThread;
        vector<utils::TimingHelper> timings;
        timings.resize(benchmarkSettings.requests);
        sendReceiver->setTimings(&timings);

        atomic<uint64_t> finishedMessages = 0;
        auto callback = [&finishedMessages, &sendReceiver](std::unique_ptr<utils::DataVector<uint8_t>> data) {
            finishedMessages++;
            sendReceiver->reuse(move(data));
        };

        random_device dev;
        mt19937 rng(dev());
        uniform_int_distribution<mt19937::result_type> dist(1, benchmarkSettings.blobFiles);
        vector<unique_ptr<typename S3SendReceiver::GetObjectRequestMessage>> requestMessages;
        if (benchmarkSettings.blobFiles > benchmarkSettings.requests) {
            for (auto i = 0u; i < benchmarkSettings.requests; i++) {
                auto filePath = benchmarkSettings.filePath + to_string(dist(rng)) + ".bin";

                auto req = typename S3SendReceiver::GetObjectRequest()
                               .WithBucket(remoteInfo.bucket)
                               .WithKey(filePath);
                requestMessages.emplace_back(make_unique<typename S3SendReceiver::template GetObjectRequestCallbackMessage<decltype(callback)>>(callback, req));
            }
        } else {
            for (auto i = 1u; i <= benchmarkSettings.blobFiles; i++) {
                auto filePath = benchmarkSettings.filePath + to_string(dist(rng)) + ".bin";

                auto req = typename S3SendReceiver::GetObjectRequest()
                               .WithBucket(remoteInfo.bucket)
                               .WithKey(filePath);
                requestMessages.emplace_back(make_unique<typename S3SendReceiver::template GetObjectRequestCallbackMessage<decltype(callback)>>(callback, req));
            }
            for (auto i = benchmarkSettings.blobFiles; i < benchmarkSettings.requests; i++) {
                auto ptr = requestMessages[i % benchmarkSettings.blobFiles].get();
                requestMessages.emplace_back(make_unique<typename S3SendReceiver::template GetObjectRequestCallbackMessage<decltype(callback)>>(callback, ptr->objectRequest));
            }
        }

        for (auto i = 0u; i < benchmarkSettings.requests; i++) {
            sendReceiver->send(requestMessages[i].get());
        }

        fstream s;
        unique_ptr<utils::Timer> timer;
        bool headerNeeded = true;
        if (benchmarkSettings.report.size()) {
            if (ifstream(benchmarkSettings.report + ".summary")) {
                headerNeeded = false;
            }
            timer = make_unique<utils::Timer>(&s, headerNeeded);
            s = fstream(benchmarkSettings.report + ".summary", s.app | s.in | s.out);
        } else {
            timer = make_unique<utils::Timer>(&cout, headerNeeded);
        }

        {
            utils::Timer::TimerGuard guard(utils::Timer::Steps::Download, timer.get());

            auto perfEventThread = [&]() {
                sendReceiver->run();
            };
            asyncSendReceiverThread = async(launch::async, perfEventThread);

            while (finishedMessages != benchmarkSettings.requests)
                usleep(100);
        }
        auto totalSize = 0ull;
        for (const auto& t : timings)
            totalSize += t.size;
        string header = ",Algorithm,Resolver,Iteration,Threads,Concurrency,Requests,Datasize,HTTPS,Encryption";
        string content = "," + to_string(iteration) + "," + to_string(benchmarkSettings.concurrentThreads) + "," + to_string(benchmarkSettings.concurrentRequests) + "," + to_string(benchmarkSettings.requests) + "," + to_string(totalSize) + "," + to_string(benchmarkSettings.https) + "," + to_string(benchmarkSettings.encryption);
        if constexpr (is_same<S3SendReceiver, network::S3CurlSendReceiver>::value)
            content = ",S3," + content;
        else
            content = ",S3Crt," + content;
        timer->setInfo(header, content);

        if (benchmarkSettings.report.size()) {
            auto report = fstream(benchmarkSettings.report, s.app | s.in | s.out);
            if (headerNeeded)
                report << "Algorithm,Resolver,Iteration,Threads,Concurrency,HTTPS,Encryption,Start,Finish,Diff,ReceiveLatency,Size" << endl;
            for (const auto& t : timings)
                if constexpr (is_same<S3SendReceiver, network::S3CurlSendReceiver>::value)
                    report << "S3,," << to_string(iteration) << "," << to_string(benchmarkSettings.concurrentThreads) << "," << to_string(benchmarkSettings.concurrentRequests) << "," << to_string(benchmarkSettings.https) << "," << to_string(benchmarkSettings.encryption) << "," << systemClockToMys(t.start) << "," << systemClockToMys(t.finish) << "," << chrono::duration_cast<chrono::microseconds>(t.finish - t.start).count() << ",0," << t.size << endl;
                else
                    report << "S3Crt,," << to_string(iteration) << "," << to_string(benchmarkSettings.concurrentThreads) << "," << to_string(benchmarkSettings.concurrentRequests) << "," << to_string(benchmarkSettings.https) << "," << to_string(benchmarkSettings.encryption) << "," << systemClockToMys(t.start) << "," << systemClockToMys(t.finish) << "," << chrono::duration_cast<chrono::microseconds>(t.finish - t.start).count() << ",0," << t.size << endl;
        }

        sendReceiver->stop();
        asyncSendReceiverThread.get();
    }
}
//---------------------------------------------------------------------------
void Bandwidth::runUring(const Settings& benchmarkSettings, const string& uri)
// The bandwith benchmark for uring interface
{
    network::TaskedSendReceiverGroup group(benchmarkSettings.chunkSize, benchmarkSettings.requests << 1);
    group.setConcurrentRequests(benchmarkSettings.concurrentRequests);
    if (benchmarkSettings.oneLake)
        group.setRequestTimeout(chrono::milliseconds(benchmarkSettings.requestTimeoutMs));
    vector<unique_ptr<network::TaskedSendReceiverHandle>> sendReceiverHandles;
    vector<unique_ptr<network::TaskedSendReceiverGroup>> taskGroups;
    for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++) {
        // taskGroups.emplace_back(make_unique<network::TaskedSendReceiverGroup>(benchmarkSettings.concurrentRequests << 16));
        sendReceiverHandles.push_back(make_unique<network::TaskedSendReceiverHandle>(group.getHandle()));
    }

    string key = "";
    if (!benchmarkSettings.rsaKeyFile.empty())
        key = cloud::Provider::getKey(benchmarkSettings.rsaKeyFile);
    unique_ptr<cloud::Provider> cloudProvider;
    if (benchmarkSettings.oneLake) {
        const char* token = getenv("AZURE_STORAGE_BEARER_TOKEN");
        if (!token || !*token)
            throw runtime_error("AZURE_STORAGE_BEARER_TOKEN is not set");
        auto info = cloud::Provider::getRemoteInfo(uri);
        info.endpoint = "onelake.blob.fabric.microsoft.com";
        info.port = 443;
        cloudProvider = make_unique<cloud::Azure>(info, cloud::Azure::BearerToken{token});
    } else {
        cloudProvider = cloud::Provider::makeProvider(uri, benchmarkSettings.https, benchmarkSettings.account, key, sendReceiverHandles.back().get());
    }

    if (cloudProvider->getType() == cloud::Provider::CloudService::AWS) {
        auto awsProvider = static_cast<cloud::AWS*>(cloudProvider.get());
        if (!benchmarkSettings.resolver.compare("aws")) {
            for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++) {
                awsProvider->initCache(*sendReceiverHandles[i].get());
            }
        }
    }

    unsigned char aesKey[] = "01234567890123456789012345678901";
    unsigned char aesIv[] = "0123456789012345";

    for (auto iteration = 0u; iteration < benchmarkSettings.iterations; iteration++) {
        const auto runId = makeRunId("uring", iteration);
        if (benchmarkSettings.oneLake && !benchmarkSettings.report.empty()) {
            validateCsvSchema(benchmarkSettings.report, DetailHeader);
            validateCsvSchema(benchmarkSettings.report + ".summary", UringSummaryHeader);
            validateCsvSchema(benchmarkSettings.report + ".manifest", ManifestHeader);
        }
        group.resetTelemetry();
        uint64_t oneLakeVerifiedBytes = 0;
        uint64_t oneLakeRetries = 0;
        vector<future<void>> asyncSendReceiverThreads;
        vector<future<void>> requestCreatorThreads;
        vector<utils::TimingHelper> timings;
        timings.resize(benchmarkSettings.requests);
        for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++) {
            sendReceiverHandles[i]->get()->setTimings(&timings);
        }

        random_device dev;
        mt19937 rng(dev());
        uniform_int_distribution<mt19937::result_type> dist(1, benchmarkSettings.blobFiles);
        pair<uint64_t, uint64_t> range = {0, 0};
        if (benchmarkSettings.oneLake)
            range = {benchmarkSettings.readOffset, benchmarkSettings.readOffset + benchmarkSettings.readLength - 1};
        atomic<uint64_t> finishedMessages = 0;
        atomic<uint64_t> failedMessages = 0;
        atomic<uint64_t> verifiedBytes = 0;
        auto enc = benchmarkSettings.encryption;

        auto callback = [&finishedMessages, &failedMessages, &verifiedBytes, &benchmarkSettings, &sendReceiverHandles, &enc, aesKey, aesIv](network::MessageResult& result) {
            if (!result.success()) {
                failedMessages++;
                cerr << "Request was not successful: " << result.getFailureCode() << endl;
                if (benchmarkSettings.oneLake)
                    cerr << "OneLake HTTP status: " << result.getResponseCodeNumber() << endl;
            } else if (benchmarkSettings.oneLake
                && (result.getResponseCodeNumber() != 206
                    || result.getSize() != benchmarkSettings.readLength
                    || [&] {
                        uint64_t contentLength = 0;
                        return !parseUnsigned(result.getResponseHeader("Content-Length"), contentLength)
                            || contentLength != benchmarkSettings.readLength;
                    }()
                    || !matchesContentRange(
                        result.getResponseHeader("Content-Range"),
                        benchmarkSettings.readOffset,
                        benchmarkSettings.readLength))) {
                failedMessages++;
                cerr << "OneLake response status, Content-Range, or byte count does not match the requested range" << endl;
            } else if (enc) {
                auto plain = sendReceiverHandles.back()->get()->getReused();
                if (!plain)
                    plain = make_unique<utils::DataVector<uint8_t>>(result.getSize() + result.getOffset());
                else
                    plain->resize(result.getSize() + result.getOffset());
                try {
                    auto len = utils::aesDecrypt(aesKey, aesIv, result.getData() + result.getOffset(), result.getSize(), plain->data());
                    plain->resize(len);
                } catch (exception& e) {
                    cerr << "Request was not successful: " << e.what() << endl;
                }
                sendReceiverHandles.back()->get()->reuse(move(plain));
            }
            if (benchmarkSettings.oneLake && result.success())
                verifiedBytes += result.getSize();
            finishedMessages++;
            sendReceiverHandles.back()->get()->reuse(result.moveDataVector());
        };

        network::Transaction getTxn[benchmarkSettings.concurrentThreads];

        for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++)
            getTxn[i].setProvider(cloudProvider.get());

        if (benchmarkSettings.oneLake) {
            for (uint64_t requestId = 0; requestId < benchmarkSettings.requests; ++requestId) {
                auto filePath = benchmarkSettings.objectPath;
                const auto threadId = requestId % benchmarkSettings.concurrentThreads;
                auto getObjectRequest = [&getTxn, &filePath, &range, threadId, requestId, callback]() {
                    return getTxn[threadId].getObjectRequest(move(callback), filePath, range, nullptr, 0, requestId);
                };
                getTxn[threadId].verifyKeyRequest(*sendReceiverHandles.back(), move(getObjectRequest));
            }
        } else if (benchmarkSettings.blobFiles > benchmarkSettings.requests) {
            for (auto i = 0u; i < benchmarkSettings.requests; i++) {
                auto filePath = benchmarkSettings.filePath + to_string(dist(rng)) + ".bin";
                auto getObjectRequest = [&getTxn, &filePath, &range, i, callback]() {
                    return getTxn[0].getObjectRequest(move(callback), filePath, range, nullptr, 0, i);
                };
                getTxn[0].verifyKeyRequest(*sendReceiverHandles.back(), move(getObjectRequest));
            }
        } else {
            auto createRequests = [&](uint64_t start, uint64_t end, uint64_t threadId) {
                for (auto i = start; i < end; i++) {
                    auto filePath = benchmarkSettings.filePath + to_string((i % benchmarkSettings.blobFiles) + 1) + ".bin";
                    auto getObjectRequest = [&getTxn, &filePath, &range, i, threadId, callback]() {
                        return getTxn[threadId].getObjectRequest(move(callback), filePath, range, nullptr, 0, i);
                    };
                    getTxn[threadId].verifyKeyRequest(*sendReceiverHandles.back(), move(getObjectRequest));
                }
            };
            auto start = 0ull;
            auto req = benchmarkSettings.requests / benchmarkSettings.concurrentThreads;
            for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++) {
                if (i != benchmarkSettings.concurrentThreads - 1) {
                    requestCreatorThreads.push_back(async(launch::async, createRequests, start, start + req, i));
                } else {
                    requestCreatorThreads.push_back(async(launch::async, createRequests, start, benchmarkSettings.requests, i));
                }
                start += req;
            }

            for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++)
                requestCreatorThreads[i].get();
        }

        for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++)
            getTxn[i].processAsync(group);

        fstream s;
        unique_ptr<utils::Timer> timer;
        bool headerNeeded = true;
        bool reportHeaderNeeded = true;
        if (benchmarkSettings.report.size()) {
            if (ifstream(benchmarkSettings.report + ".summary")) {
                headerNeeded = false;
            }
            if (ifstream(benchmarkSettings.report))
                reportHeaderNeeded = false;
            if (benchmarkSettings.oneLake) {
                if (!headerNeeded) {
                    validateCsvSchema(benchmarkSettings.report + ".summary", UringSummaryHeader);
                }
                if (!reportHeaderNeeded)
                    validateCsvSchema(benchmarkSettings.report, DetailHeader);
            }
            timer = make_unique<utils::Timer>(&s, headerNeeded);
            s = fstream(benchmarkSettings.report + ".summary", s.app | s.in | s.out);
        } else {
            timer = make_unique<utils::Timer>(&cout, headerNeeded);
        }

        {
            utils::Timer::TimerGuard guard(utils::Timer::Steps::Download, timer.get());

            for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++) {
                auto perfEventThread = [&](uint64_t recv) {
                    sendReceiverHandles[recv]->process(false);
                };
                asyncSendReceiverThreads.push_back(async(launch::async, perfEventThread, i));
            }

            while (finishedMessages != benchmarkSettings.requests)
                usleep(100);
        }

        if (benchmarkSettings.oneLake) {
            for (auto& handle : sendReceiverHandles)
                handle->stop();
            for (auto& worker : asyncSendReceiverThreads)
                worker.get();
            if (failedMessages || verifiedBytes != benchmarkSettings.requests * benchmarkSettings.readLength) {
                timer->setOutStream(nullptr);
                throw runtime_error("OneLake read validation failed; no successful benchmark result was recorded");
            }
            const auto retries = group.getRetryAttempts();
            oneLakeVerifiedBytes = verifiedBytes;
            oneLakeRetries = retries;
            cout << "OneLake AnyBlob completed: requests=" << finishedMessages
                 << " attempts=" << finishedMessages + retries << " retries=" << retries
                 << " bytes=" << verifiedBytes << " failures=" << failedMessages
                 << " max_concurrency=" << group.getObservedConcurrency() << endl;
        }

        if (benchmarkSettings.testUpload) {
            finishedMessages = 0;

            auto callbackUpload = [&finishedMessages](network::MessageResult& result) {
                if (!result.success())
                    cerr << "Request was not successful: " << result.getFailureCode() << endl;
                finishedMessages++;
            };

            network::Transaction putTxn(cloudProvider.get());
            auto blob = make_unique<utils::DataVector<uint8_t>>(1 << 24);
            if (benchmarkSettings.encryption) {
                auto plainBlob = make_unique<utils::DataVector<uint8_t>>((1 << 24) - 16);
                try {
                    auto len = utils::aesEncrypt(aesKey, aesIv, plainBlob->cdata(), plainBlob->size(), blob->data());
                    blob->resize(len);
                } catch (exception& e) {
                    cerr << "Encryption unsuccessfult: " << e.what() << endl;
                }
            }
            {
                uint64_t requestPerSocket = static_cast<double>(benchmarkSettings.requests) / benchmarkSettings.concurrentThreads;
                for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++) {
                    auto start = i * requestPerSocket;
                    auto end = (i + 1) * requestPerSocket;
                    if (i == benchmarkSettings.concurrentThreads - 1)
                        end = benchmarkSettings.requests;
                    for (uint64_t j = start; j < end; j++) {
                        auto filePath = "upload_" + benchmarkSettings.filePath + to_string(j + 1) + ".bin";
                        auto putObjectRequest = [&putTxn, &filePath, callbackUpload, &blob, i]() {
                            return putTxn.putObjectRequest(move(callbackUpload), filePath, reinterpret_cast<const char*>(blob->data()), blob->size(), nullptr, 0, i);
                        };
                        putTxn.verifyKeyRequest(*sendReceiverHandles.back(), move(putObjectRequest));
                    }
                }

                utils::Timer::TimerGuard guard(utils::Timer::Steps::Upload, timer.get());

                for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++) {
                    sendReceiverHandles[i]->get()->setTimings(nullptr);
                }

                putTxn.processAsync(group);

                while (finishedMessages != requestPerSocket * benchmarkSettings.concurrentThreads)
                    usleep(100);
            }
        }

        auto totalSize = 0ull;
        for (const auto& t : timings)
            totalSize += t.size;

        string header = ",Algorithm,Resolver,Iteration,Threads,Concurrency,Requests,Datasize,HTTPS,Encryption,RunId";
        string content = ",Uring," + string(benchmarkSettings.resolver) + "," + to_string(iteration) + "," + to_string(benchmarkSettings.concurrentThreads) + "," + to_string(benchmarkSettings.concurrentRequests) + "," + to_string(benchmarkSettings.requests) + "," + to_string(totalSize) + "," + to_string(benchmarkSettings.https) + "," + to_string(benchmarkSettings.encryption) + "," + runId;
        timer->setInfo(header, content);

        if (benchmarkSettings.report.size()) {
            auto report = fstream(benchmarkSettings.report, s.app | s.in | s.out);
            if (reportHeaderNeeded)
                report << "Algorithm,Resolver,Iteration,Threads,Concurrency,HTTPS,Encryption,Start,Finish,Diff,ReceiveLatency,Size,RunId" << endl;
            for (const auto& t : timings)
                if (t.size > 0)
                    report << "Uring," + string(benchmarkSettings.resolver) << "," << to_string(iteration) << "," << to_string(benchmarkSettings.concurrentThreads) << "," << to_string(benchmarkSettings.concurrentRequests) << "," << to_string(benchmarkSettings.https) << "," << to_string(benchmarkSettings.encryption) << "," << systemClockToMys(t.start) << "," << systemClockToMys(t.finish) << "," << chrono::duration_cast<chrono::microseconds>(t.finish - t.start).count() << "," << chrono::duration_cast<chrono::microseconds>(t.recieve - t.start).count() << "," << t.size << "," << runId << endl;
        }
        for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++)
            sendReceiverHandles[i]->stop();

        for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++)
            if (asyncSendReceiverThreads[i].valid())
                asyncSendReceiverThreads[i].get();
        timer.reset();
        if (benchmarkSettings.oneLake && !benchmarkSettings.report.empty()) {
            appendRunManifest(
                benchmarkSettings.report,
                runId,
                "Uring",
                iteration,
                benchmarkSettings.requests,
                benchmarkSettings.requests + oneLakeRetries,
                oneLakeVerifiedBytes,
                0,
                static_cast<uint64_t>(benchmarkSettings.concurrentThreads) * benchmarkSettings.concurrentRequests,
                group.getObservedConcurrency());
        }
    }
}
//---------------------------------------------------------------------------
} // namespace anyblob::benchmark
