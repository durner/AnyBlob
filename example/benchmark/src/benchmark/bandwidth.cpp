#include "benchmark/bandwidth.hpp"
#include "cloud/aws.hpp"
#include "cloud/aws_resolver.hpp"
#include "cloud/azure.hpp"
#include "cloud/gcp.hpp"
#include "network/messages.hpp"
#include "network/s3_send_receiver.hpp"
#include "network/tasked_send_receiver.hpp"
#include "perfevent/PerfEvent.hpp"
#include "utils/timer.hpp"
#include <fstream>
#include <future>
#include <iostream>
#include <random>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace benchmark {
//---------------------------------------------------------------------------
using namespace std;
using namespace anyblob;
//---------------------------------------------------------------------------
void Bandwidth::run(const Settings& benchmarkSettings, cloud::Provider& cloudProvider)
/// The bandwith benchmark
{
    for (auto s : benchmarkSettings.systems) {
        switch (s) {
            case Systems::Uring:
                runUring(benchmarkSettings, cloudProvider);
                break;
            case Systems::S3Crt:
                runS3<network::S3CrtSendReceiver>(benchmarkSettings, cloudProvider);
                break;
            case Systems::S3:
                runS3<network::S3CurlSendReceiver>(benchmarkSettings, cloudProvider);
                break;
        }
    }
}
//---------------------------------------------------------------------------
template <typename S3SendReceiver>
void Bandwidth::runS3(const Settings& benchmarkSettings, cloud::Provider& cloudProvider)
/// The bandwith benchmark for S3 interface
{
    auto awsPtr = dynamic_cast<cloud::AWS*>(&cloudProvider);
    auto settings = awsPtr->getSettings();

    unique_ptr<S3SendReceiver> sendReceiver = make_unique<S3SendReceiver>(benchmarkSettings.requests << 1, benchmarkSettings.concurrentRequests, settings, benchmarkSettings.concurrentThreads, benchmarkSettings.https);

    for (auto iteration = 0u; iteration < benchmarkSettings.iterations; iteration++) {
        future<void> asyncSendReceiverThread;
        vector<utils::TimingHelper> timings;
        timings.resize(benchmarkSettings.requests);
        sendReceiver->setTimings(&timings);

        Aws::IOStreamFactory streamFactory([]() {
            return Aws::New<Aws::FStream>("S3Client", "/dev/null", ios_base::out);
        });

        std::atomic<uint64_t> finishedMessages = 0;
        auto callback = [&finishedMessages](std::unique_ptr<utils::DataVector<uint8_t>> /*data*/) {
            finishedMessages++;
        };

        random_device dev;
        mt19937 rng(dev());
        uniform_int_distribution<mt19937::result_type> dist(1, benchmarkSettings.blobFiles);
        vector<unique_ptr<typename S3SendReceiver::GetObjectRequestMessage>> requestMessages;
        if (benchmarkSettings.blobFiles > benchmarkSettings.requests) {
            for (auto i = 0u; i < benchmarkSettings.requests; i++) {
                auto filePath = benchmarkSettings.filePath + to_string(dist(rng)) + ".bin";

                auto req = typename S3SendReceiver::GetObjectRequest()
                               .WithBucket(settings.bucket)
                               .WithKey(filePath);
                req.SetResponseStreamFactory(streamFactory);
                requestMessages.emplace_back(make_unique<typename S3SendReceiver::template GetObjectRequestCallbackMessage<decltype(callback)>>(callback, req));
            }
        } else {
            for (auto i = 1u; i <= benchmarkSettings.blobFiles; i++) {
                auto filePath = benchmarkSettings.filePath + to_string(dist(rng)) + ".bin";

                auto req = typename S3SendReceiver::GetObjectRequest()
                               .WithBucket(settings.bucket)
                               .WithKey(filePath);
                req.SetResponseStreamFactory(streamFactory);
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
        string header = ",Algorithm,Resolver,Iteration,Threads,Concurrency,Requests,Datasize,HTTPS";
        string content = "," + to_string(iteration) + "," + to_string(benchmarkSettings.concurrentThreads) + "," + to_string(benchmarkSettings.concurrentRequests) + "," + to_string(benchmarkSettings.requests) + "," + to_string(totalSize) + "," + to_string(benchmarkSettings.https);
        if constexpr (is_same<S3SendReceiver, network::S3CurlSendReceiver>::value)
            content = ",S3," + content;
        else
            content = ",S3Crt," + content;
        timer->setInfo(header, content);

        if (benchmarkSettings.report.size()) {
            auto report = fstream(benchmarkSettings.report, s.app | s.in | s.out);
            if (headerNeeded)
                report << "Algorithm,Resolver,Iteration,Threads,Concurrency,Start,Finish,Diff,Size" << std::endl;
            for (const auto& t : timings)
                if constexpr (is_same<S3SendReceiver, network::S3CurlSendReceiver>::value)
                    report << "S3,," << to_string(iteration) << "," << to_string(benchmarkSettings.concurrentThreads) << "," << to_string(benchmarkSettings.concurrentRequests) << "," << systemClockToMys(t.start) << "," << systemClockToMys(t.finish) << "," << std::chrono::duration_cast<std::chrono::microseconds>(t.finish - t.start).count() << "," << t.size << std::endl;
                else
                    report << "S3Crt,," << to_string(iteration) << "," << to_string(benchmarkSettings.concurrentThreads) << "," << to_string(benchmarkSettings.concurrentRequests) << "," << systemClockToMys(t.start) << "," << systemClockToMys(t.finish) << "," << std::chrono::duration_cast<std::chrono::microseconds>(t.finish - t.start).count() << "," << t.size << std::endl;
        }

        sendReceiver->stop();
        asyncSendReceiverThread.get();
    }
}
//---------------------------------------------------------------------------
void Bandwidth::runUring(const Settings& benchmarkSettings, cloud::Provider& cloudProvider)
/// The bandwith benchmark for uring interface
{
    network::TaskedSendReceiverGroup group(benchmarkSettings.concurrentRequests, benchmarkSettings.requests << 1, benchmarkSettings.chunkSize);
    vector<unique_ptr<network::TaskedSendReceiver>> sendReceivers;
    vector<unique_ptr<network::TaskedSendReceiverGroup>> taskGroups;
    for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++) {
        // taskGroups.emplace_back(make_unique<network::TaskedSendReceiverGroup>(benchmarkSettings.concurrentRequests << 16));
        sendReceivers.push_back(make_unique<network::TaskedSendReceiver>(group));
    }

    if (cloudProvider.getType() == cloud::Provider::CloudService::AWS) {
        auto awsProvider = static_cast<cloud::AWS*>(&cloudProvider);
        awsProvider->initSecret(*sendReceivers.back());
        if (!benchmarkSettings.resolver.compare("aws")) {
            for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++) {
                awsProvider->initResolver(*sendReceivers[i].get());
            }
        }
    } else if (cloudProvider.getType() == cloud::Provider::CloudService::GCP) {
        auto gcpProvider = static_cast<cloud::GCP*>(&cloudProvider);
        gcpProvider->initKey();
    } else if (cloudProvider.getType() == cloud::Provider::CloudService::Azure) {
        auto azureProvider = static_cast<cloud::Azure*>(&cloudProvider);
        azureProvider->initKey();
    }

    for (auto iteration = 0u; iteration < benchmarkSettings.iterations; iteration++) {
        vector<future<void>> asyncSendReceiverThreads;
        vector<utils::TimingHelper> timings;
        timings.resize(benchmarkSettings.requests);
        for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++) {
            sendReceivers[i]->setTimings(&timings);
        }

        random_device dev;
        mt19937 rng(dev());
        uniform_int_distribution<mt19937::result_type> dist(1, benchmarkSettings.blobFiles);
        vector<unique_ptr<network::OriginalMessage>> requestMessages;
        pair<uint64_t, uint64_t> range = {0, 0};
        std::atomic<uint64_t> finishedMessages = 0;

        auto callback = [&finishedMessages, &sendReceivers](std::unique_ptr<utils::DataVector<uint8_t>> data) {
            finishedMessages++;
            sendReceivers.back()->reuse(move(data));
        };

        if (benchmarkSettings.blobFiles > benchmarkSettings.requests) {
            for (auto i = 0u; i <= benchmarkSettings.requests; i++) {
                auto filePath = benchmarkSettings.filePath + to_string(dist(rng)) + ".bin";
                auto message = cloudProvider.getRequest(filePath, range);
                requestMessages.emplace_back(make_unique<network::OriginalCallbackMessage<decltype(callback)>>(callback, move(message), cloudProvider.getAddress(), cloudProvider.getPort(), nullptr, 0, i));

                //auto httpHeader = string("GET /1GB.bin HTTP/1.1\r\n") + "Host: ip-172-31-7-174.eu-central-1.compute.internal" + "\r\n\r\n";
                //auto message = make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
                //requestMessages.emplace_back(make_unique<network::OriginalMessage>(move(message), "ip-172-31-7-174.eu-central-1.compute.internal", cloudProvider.getPort()));
            }
        } else {
            for (auto i = 1u; i <= benchmarkSettings.blobFiles; i++) {
                auto filePath = benchmarkSettings.filePath + to_string(i) + ".bin";
                auto message = cloudProvider.getRequest(filePath, range);
                requestMessages.emplace_back(make_unique<network::OriginalCallbackMessage<decltype(callback)>>(callback, move(message), cloudProvider.getAddress(), cloudProvider.getPort(), nullptr, 0, i - 1));

                //auto httpHeader = string("GET /1GB.bin HTTP/1.1\r\n") + "Host: ip-172-31-7-174.eu-central-1.compute.internal" + "\r\n\r\n";
                //auto message = make_unique<utils::DataVector<uint8_t>>(reinterpret_cast<uint8_t*>(httpHeader.data()), reinterpret_cast<uint8_t*>(httpHeader.data() + httpHeader.size()));
                //requestMessages.emplace_back(make_unique<network::OriginalMessage>(move(message), "ip-172-31-7-174.eu-central-1.compute.internal", cloudProvider.getPort()));
            }
            for (auto i = benchmarkSettings.blobFiles + 1; i <= benchmarkSettings.requests; i++) {
                auto ptr = requestMessages[i % benchmarkSettings.blobFiles]->message.get();
                requestMessages.emplace_back(make_unique<network::OriginalCallbackMessage<decltype(callback)>>(callback, make_unique<utils::DataVector<uint8_t>>(*ptr), cloudProvider.getAddress(), cloudProvider.getPort(), nullptr, 0, i - 1));
            }
        }

        for (auto i = 0u; i < requestMessages.size(); i++) {
            if (!requestMessages[i].get())
                abort();
            sendReceivers.front()->send(requestMessages[i].get());
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

            for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++) {
                auto perfEventThread = [&](uint64_t recv) {
                    sendReceivers[recv]->run();
                };
                asyncSendReceiverThreads.push_back(async(launch::async, perfEventThread, i));
            }

            while (finishedMessages != requestMessages.size())
                usleep(100);
        }

        if (benchmarkSettings.testUpload) {
            finishedMessages = 0;
            requestMessages.clear();
            auto blob = make_unique<utils::DataVector<uint8_t>>(1 << 24);
            {
                uint64_t requestPerSocket = static_cast<double>(benchmarkSettings.requests) / benchmarkSettings.concurrentThreads;
                for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++) {
                    auto start = i * requestPerSocket;
                    auto end = (i + 1) * requestPerSocket;
                    for (uint64_t j = start; j < end; j++) {
                        auto filePath = "upload_" + benchmarkSettings.filePath + to_string(dist(rng)) + ".bin";
                        auto message = cloudProvider.putRequest(filePath, blob->data(), blob->size());
                        requestMessages.emplace_back(make_unique<network::OriginalCallbackMessage<decltype(callback)>>(callback, move(message), cloudProvider.getAddress(), cloudProvider.getPort()));
                        requestMessages.back()->setPutRequestData(blob->data(), blob->size());
                    }
                }

                utils::Timer::TimerGuard guard(utils::Timer::Steps::Upload, timer.get());

                for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++) {
                    sendReceivers[i]->setTimings(nullptr);
                }

                for (auto i = 0u; i < requestMessages.size(); i++) {
                    if (!requestMessages[i].get())
                        abort();
                    sendReceivers.front()->send(requestMessages[i].get());
                }

                while (finishedMessages != requestMessages.size())
                    usleep(100);
            }
        }

        auto totalSize = 0ull;
        for (const auto& t : timings)
            totalSize += t.size;

        string header = ",Algorithm,Resolver,Iteration,Threads,Concurrency,Requests,Datasize,HTTPS";
        string content = ",Uring," + string(benchmarkSettings.resolver) + "," + to_string(iteration) + "," + to_string(benchmarkSettings.concurrentThreads) + "," + to_string(benchmarkSettings.concurrentRequests) + "," + to_string(benchmarkSettings.requests) + "," + to_string(totalSize) + "," + to_string(benchmarkSettings.https);
        timer->setInfo(header, content);

        if (benchmarkSettings.report.size()) {
            auto report = fstream(benchmarkSettings.report, s.app | s.in | s.out);
            if (headerNeeded)
                report << "Algorithm,Resolver,Iteration,Threads,Concurrency,Start,Finish,Diff,ReceiveLatency,Size" << std::endl;
            for (const auto& t : timings)
                if (t.size > 0)
                    report << "Uring," + string(benchmarkSettings.resolver) << "," << to_string(iteration) << "," << to_string(benchmarkSettings.concurrentThreads) << "," << to_string(benchmarkSettings.concurrentRequests) << "," << systemClockToMys(t.start) << "," << systemClockToMys(t.finish) << "," << std::chrono::duration_cast<std::chrono::microseconds>(t.finish - t.start).count() << "," << std::chrono::duration_cast<std::chrono::microseconds>(t.recieve - t.start).count() << "," << t.size << std::endl;
        }
        for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++)
            sendReceivers[i]->stop();

        for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++)
            asyncSendReceiverThreads[i].get();
    }
}
//---------------------------------------------------------------------------
}; // namespace benchmark
}; // namespace anyblob
