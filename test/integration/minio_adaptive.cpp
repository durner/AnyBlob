#include "catch2/single_include/catch2/catch.hpp"
#include "cloud/provider.hpp"
#include "network/adaptive_controller.hpp"
#include "network/tasked_send_receiver.hpp"
#include "network/transaction.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <vector>
#include <unistd.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2026
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::test {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
TEST_CASE("MinIO Adaptive Integration") {
    // Read connection settings
    const char* bucket = getenv("AWS_S3_BUCKET");
    const char* region = getenv("AWS_S3_REGION");
    const char* endpoint = getenv("AWS_S3_ENDPOINT");
    const char* key = getenv("AWS_S3_ACCESS_KEY");
    const char* secret = getenv("AWS_S3_SECRET_ACCESS_KEY");

    REQUIRE(bucket);
    REQUIRE(region);
    REQUIRE(endpoint);
    REQUIRE(key);
    REQUIRE(secret);

    auto stringGen = [](size_t len) {
        static constexpr auto chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        auto resultString = string(len, '\0');
        generate_n(begin(resultString), len, [&]() { return chars[static_cast<unsigned>(rand()) % strlen(chars)]; });
        return resultString;
    };

    // Test files and content
    string bucketName = "minio://";
    bucketName = bucketName + endpoint + "/" + bucket + ":" + region;
    string fileName[]{"adaptive_first.txt", "adaptive_second.txt"};
    string content[]{stringGen(1 << 20), stringGen(1 << 20)};
    // Requests per download round
    static constexpr auto requests = 64u;
    // Fixed hardware model for reproducible tests
    static constexpr auto modeledThreads = 16u;
    // Daemon limit for this test
    static constexpr auto maxDaemons = 4u;
    // Run load phases for two measurement epochs
    static constexpr auto loadEpochs = 2 * anyblob::network::AdaptiveController::epochLength;

    // Create the shared task group
    anyblob::network::TaskedSendReceiverGroup group(64u * 1024, requests << 2, 0);

    // Get a scheduler handle
    auto sendReceiverHandle = group.getHandle();

    // Create the bucket provider
    auto provider = anyblob::cloud::Provider::makeProvider(bucketName, false, key, secret, &sendReceiverHandle);

    // Seed from hardware; MinIO reports no bandwidth
    auto config = provider->getConfig(sendReceiverHandle);
    anyblob::network::AdaptiveController controller(config, false, modeledThreads);

    // Start with multiple daemons to test idle reduction
    REQUIRE(controller.current().threads > 1);
    REQUIRE(controller.maxThreads() == modeledThreads);

    // Worker handles and futures
    vector<anyblob::network::TaskedSendReceiverHandle> sendReceiverHandles;
    vector<future<void>> asyncSendReceiverThreads(maxDaemons);
    sendReceiverHandles.reserve(maxDaemons);
    for (auto i = 0u; i < maxDaemons; i++)
        sendReceiverHandles.push_back(group.getHandle());

    // Use counters in worker callbacks; Catch2 is not thread safe
    auto createDownloads = [&](anyblob::network::Transaction& getTxn, atomic<uint16_t>& finishedMessages, atomic<uint16_t>& validMessages) {
        for (auto i = 0u; i < requests; i++) {
            auto file = i % 2;
            auto checkSuccess = [&finishedMessages, &validMessages, &content, file](anyblob::network::MessageResult& result) {
                if (result.success() && result.getSize() == content[file].size() && !content[file].compare(result.getResult()))
                    validMessages++;
                finishedMessages++;
            };

            auto& currentFileName = fileName[file];
            auto getObjectRequest = [&getTxn, &currentFileName, callback = move(checkSuccess)]() {
                return getTxn.getObjectRequest(move(callback), currentFileName);
            };
            getTxn.verifyKeyRequest(sendReceiverHandle, move(getObjectRequest));
        }
    };

    {
        // Create upload requests
        anyblob::network::Transaction putTxn(provider.get());
        for (auto i = 0u; i < 2; i++) {
            auto putObjectRequest = [&putTxn, &fileName, &content, i]() {
                return putTxn.putObjectRequest(fileName[i], content[i].data(), content[i].size());
            };
            putTxn.verifyKeyRequest(sendReceiverHandle, move(putObjectRequest));
        }

        // Upload synchronously
        putTxn.processSync(sendReceiverHandle);

        // Check the upload
        for (const auto& it : putTxn) {
            REQUIRE(it.success());
        }
    }

    SECTION("the controller sizes the daemons of a burst") {
        auto bursts = 0u;
        auto running = 0u;
        auto start = chrono::steady_clock::now();
        while (chrono::steady_clock::now() - start < loadEpochs) {
            atomic<uint16_t> finishedMessages = 0;
            atomic<uint16_t> validMessages = 0;

            // Create download requests
            anyblob::network::Transaction getTxn(provider.get());
            createDownloads(getTxn, finishedMessages, validMessages);

            // Queue work first; daemons exit on an empty queue
            REQUIRE(getTxn.processAsync(group));

            // Apply the recommendation for this burst
            auto recommendation = controller.recommend(group, running);
            group.setConcurrentRequests(recommendation.requestsPerThread);
            auto daemons = min(recommendation.threads, maxDaemons);
            running = daemons;

            // Start daemons that exit when the queue empties
            for (auto i = 0u; i < daemons; i++) {
                auto runLambda = [&sendReceiverHandles](unsigned recv) {
                    sendReceiverHandles[recv].process(true);
                };
                asyncSendReceiverThreads[i] = async(launch::async, runLambda, i);
            }

            // Wait for daemons to exit on their own
            for (auto i = 0u; i < daemons; i++)
                asyncSendReceiverThreads[i].get();

            // Check the download
            REQUIRE(finishedMessages == requests);
            REQUIRE(validMessages == requests);
            REQUIRE(group.getQueuedMessages() == 0);
            REQUIRE(group.getInflightMessages() == 0);
            bursts++;
        }
        REQUIRE(bursts > 0);
    }

    SECTION("an epoch without work falls back to a single daemon") {
        // Measure an idle group
        auto running = 0u;
        auto recommendation = controller.current();
        // Wait for the recommendation to drop to one thread
        while (recommendation.threads != 1) {
            recommendation = controller.recommend(group, running);
            usleep(1000);
        }
        REQUIRE(recommendation.requestsPerThread >= 1);
    }

    SECTION("resizing a running pool answers every request") {
        auto runningDaemons = 0u;

        // Resize the persistent daemon pool
        auto reconcileDaemons = [&](unsigned daemons) {
            auto target = min(daemons, maxDaemons);
            while (runningDaemons < target) {
                auto runLambda = [&sendReceiverHandles](unsigned recv) {
                    // Keep running when the queue empties
                    sendReceiverHandles[recv].process(false);
                };
                asyncSendReceiverThreads[runningDaemons] = async(launch::async, runLambda, runningDaemons);
                runningDaemons++;
            }
            while (runningDaemons > target) {
                runningDaemons--;
                sendReceiverHandles[runningDaemons].stop();
                asyncSendReceiverThreads[runningDaemons].get();
            }
        };

        atomic<uint16_t> finishedMessages = 0;
        atomic<uint16_t> validMessages = 0;

        // Create download requests
        anyblob::network::Transaction getTxn(provider.get());
        createDownloads(getTxn, finishedMessages, validMessages);

        // Queue downloads
        REQUIRE(getTxn.processAsync(group));

        // Vary pool size and request limits; removed daemons drain first
        unsigned daemonSteps[]{1, maxDaemons, 2, maxDaemons, 1};
        unsigned requestSteps[]{1, 32, 4, 16, 8};
        for (auto step = 0u; step < 10; step++) {
            group.setConcurrentRequests(min(requestSteps[step % 5], anyblob::network::TaskedSendReceiverGroup::maxConcurrentRequests));
            reconcileDaemons(daemonSteps[step % 5]);
        }

        // Wait for the download
        while (finishedMessages != requests)
            usleep(100);

        // Stop daemons before checking callback counts
        reconcileDaemons(0);
        REQUIRE(finishedMessages == requests);
        REQUIRE(validMessages == requests);
        REQUIRE(group.getQueuedMessages() == 0);
        REQUIRE(group.getInflightMessages() == 0);
    }

    {
        // Create delete requests
        anyblob::network::Transaction deleteTxn(provider.get());
        for (auto& currentFileName : fileName) {
            auto deleteRequest = [&deleteTxn, &currentFileName]() {
                return deleteTxn.deleteObjectRequest(currentFileName);
            };
            deleteTxn.verifyKeyRequest(sendReceiverHandle, move(deleteRequest));
        }

        // Delete synchronously
        deleteTxn.processSync(sendReceiverHandle);

        // Check the deletion
        for (const auto& it : deleteTxn) {
            REQUIRE(it.success());
        }
    }

    REQUIRE(group.getInflightMessages() == 0);
}
//---------------------------------------------------------------------------
} // namespace anyblob::test
