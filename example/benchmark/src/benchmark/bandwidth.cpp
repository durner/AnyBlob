#include "benchmark/bandwidth.hpp"
#include "cloud/aws.hpp"
#include "cloud/aws_resolver.hpp"
#include "cloud/azure.hpp"
#include "cloud/gcp.hpp"
#include "network/original_message.hpp"
#include "network/s3_send_receiver.hpp"
#include "network/tasked_send_receiver.hpp"
#include "network/transaction.hpp"
#include "perfevent/PerfEvent.hpp"
#include "utils/timer.hpp"
#include "utils/utils.hpp"
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
    vector<unique_ptr<network::TaskedSendReceiverHandle>> sendReceiverHandles;
    vector<unique_ptr<network::TaskedSendReceiverGroup>> taskGroups;
    for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++) {
        // taskGroups.emplace_back(make_unique<network::TaskedSendReceiverGroup>(benchmarkSettings.concurrentRequests << 16));
        sendReceiverHandles.push_back(make_unique<network::TaskedSendReceiverHandle>(group.getHandle()));
    }

    string key = "";
    if (!benchmarkSettings.rsaKeyFile.empty())
        key = cloud::Provider::getKey(benchmarkSettings.rsaKeyFile);
    auto cloudProvider = cloud::Provider::makeProvider(uri, benchmarkSettings.https, benchmarkSettings.account, key, sendReceiverHandles.back().get());

    if (cloudProvider->getType() == cloud::Provider::CloudService::AWS) {
        auto awsProvider = static_cast<cloud::AWS*>(cloudProvider.get());
        if (!benchmarkSettings.resolver.compare("aws")) {
            for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++) {
                awsProvider->initResolver(*sendReceiverHandles[i].get());
            }
        }
    }

    unsigned char aesKey[] = "01234567890123456789012345678901";
    unsigned char aesIv[] = "0123456789012345";

    for (auto iteration = 0u; iteration < benchmarkSettings.iterations; iteration++) {
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
        atomic<uint64_t> finishedMessages = 0;
        auto enc = benchmarkSettings.encryption;

        auto callback = [&finishedMessages, &sendReceiverHandles, &enc, aesKey, aesIv](network::MessageResult& result) {
            if (!result.success()) {
                cerr << "Request was not successful: " << result.getFailureCode() << endl;
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
            finishedMessages++;
            sendReceiverHandles.back()->get()->reuse(result.moveDataVector());
        };

        network::Transaction getTxn[benchmarkSettings.concurrentThreads];

        for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++)
            getTxn[i].setProvider(cloudProvider.get());

        if (benchmarkSettings.blobFiles > benchmarkSettings.requests) {
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
                    sendReceiverHandles[recv]->process(false);
                };
                asyncSendReceiverThreads.push_back(async(launch::async, perfEventThread, i));
            }

            while (finishedMessages != benchmarkSettings.requests)
                usleep(100);
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

        string header = ",Algorithm,Resolver,Iteration,Threads,Concurrency,Requests,Datasize,HTTPS,Encryption";
        string content = ",Uring," + string(benchmarkSettings.resolver) + "," + to_string(iteration) + "," + to_string(benchmarkSettings.concurrentThreads) + "," + to_string(benchmarkSettings.concurrentRequests) + "," + to_string(benchmarkSettings.requests) + "," + to_string(totalSize) + "," + to_string(benchmarkSettings.https) + "," + to_string(benchmarkSettings.encryption);
        timer->setInfo(header, content);

        if (benchmarkSettings.report.size()) {
            auto report = fstream(benchmarkSettings.report, s.app | s.in | s.out);
            if (headerNeeded)
                report << "Algorithm,Resolver,Iteration,Threads,Concurrency,HTTPS,Encryption,Start,Finish,Diff,ReceiveLatency,Size" << endl;
            for (const auto& t : timings)
                if (t.size > 0)
                    report << "Uring," + string(benchmarkSettings.resolver) << "," << to_string(iteration) << "," << to_string(benchmarkSettings.concurrentThreads) << "," << to_string(benchmarkSettings.concurrentRequests) << "," << to_string(benchmarkSettings.https) << "," << to_string(benchmarkSettings.encryption) << "," << systemClockToMys(t.start) << "," << systemClockToMys(t.finish) << "," << chrono::duration_cast<chrono::microseconds>(t.finish - t.start).count() << "," << chrono::duration_cast<chrono::microseconds>(t.recieve - t.start).count() << "," << t.size << endl;
        }
        for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++)
            sendReceiverHandles[i]->stop();

        for (auto i = 0u; i < benchmarkSettings.concurrentThreads; i++)
            asyncSendReceiverThreads[i].get();
    }
}
//---------------------------------------------------------------------------
}; // namespace benchmark
}; // namespace anyblob
