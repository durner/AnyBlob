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
using namespace std::chrono_literals;
//---------------------------------------------------------------------------
TEST_CASE("MinIO Adaptive Integration") {
    // Get the enviornment
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

    // The files to be uploaded and downloaded
    string bucketName = "minio://";
    bucketName = bucketName + endpoint + "/" + bucket + ":" + region;
    string fileName[]{"adaptive_first.txt", "adaptive_second.txt"};
    string content[]{stringGen(1 << 20), stringGen(1 << 20)};
    // The outstanding requests of one download round
    static constexpr auto requests = 64u;
    // The machine the controller is modeled on, the host must not change the run
    static constexpr auto modeledThreads = 16u;
    // The daemons the test is willing to start of a larger recommendation
    static constexpr auto maxDaemons = 4u;
    // The controller measures in epochs, so the load phases are counted in them
    static constexpr auto loadEpochs = 2 * anyblob::network::AdaptiveController::epochLength;
    // Bounds that only catch a stuck run, no check depends on their value
    static constexpr auto requestLimit = 60s;
    static constexpr auto collapseLimit = 10 * anyblob::network::AdaptiveController::epochLength;

    // Create a new task group that bounds the requests of a single daemon
    anyblob::network::TaskedSendReceiverGroup group(64u * 1024, requests << 2, 0, 32);

    // Create an AnyBlob scheduler object for the group
    auto sendReceiverHandle = group.getHandle();

    // Create the provider for the corresponding filename
    auto provider = anyblob::cloud::Provider::makeProvider(bucketName, false, key, secret, &sendReceiverHandle);

    // Seed the controller according to instance settings (hardware estimate here, as minio reports no bandwidth)
    auto config = provider->getConfig(sendReceiverHandle);
    auto seed = anyblob::network::AdaptiveController::seedThreads(config, false, modeledThreads);
    anyblob::network::AdaptiveController controller(group.maxConcurrentRequests(), seed, modeledThreads);

    // The seeded daemons must be able to fall back to a single one
    REQUIRE(controller.current().threads > 1);
    REQUIRE(controller.maxThreads() == modeledThreads);

    // The daemons that work off the group
    vector<anyblob::network::TaskedSendReceiverHandle> sendReceiverHandles;
    vector<future<void>> asyncSendReceiverThreads(maxDaemons);
    for (auto i = 0u; i < maxDaemons; i++)
        sendReceiverHandles.push_back(group.getHandle());

    // Create a round of downloads, the callbacks run on the daemons and Catch2 is not thread safe
    auto createDownloads = [&](anyblob::network::Transaction& getTxn, atomic<uint16_t>& finishedMessages, atomic<uint16_t>& validMessages) {
        for (auto i = 0u; i < requests; i++) {
            auto file = i % 2;
            // Check the download for success
            auto checkSuccess = [&finishedMessages, &validMessages, &content, file](anyblob::network::MessageResult& result) {
                // Sucessful request with the uploaded content
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
        // Create the put request
        anyblob::network::Transaction putTxn(provider.get());
        for (auto i = 0u; i < 2; i++) {
            auto putObjectRequest = [&putTxn, &fileName, &content, i]() {
                return putTxn.putObjectRequest(fileName[i], content[i].data(), content[i].size());
            };
            putTxn.verifyKeyRequest(sendReceiverHandle, move(putObjectRequest));
        }

        // Upload the request synchronously with the scheduler object on this thread
        putTxn.processSync(sendReceiverHandle);

        // Check the upload
        for (const auto& it : putTxn) {
            // Sucessful request
            REQUIRE(it.success());
        }
    }

    SECTION("the controller sizes the daemons of a burst") {
        auto bursts = 0u;
        auto start = chrono::steady_clock::now();
        while (chrono::steady_clock::now() - start < loadEpochs) {
            atomic<uint16_t> finishedMessages = 0;
            atomic<uint16_t> validMessages = 0;

            // Create the get request
            anyblob::network::Transaction getTxn(provider.get());
            createDownloads(getTxn, finishedMessages, validMessages);

            // Queue the requests before the daemons start, they would exit on an empty queue
            REQUIRE(getTxn.processAsync(group));

            // Ask the controller how to work off the burst
            auto recommendation = controller.recommend(group);
            group.setConcurrentRequests(recommendation.requestsPerThread);
            auto daemons = min(recommendation.threads, maxDaemons);

            // Start the daemons, they end as soon as the group runs out of work
            for (auto i = 0u; i < daemons; i++) {
                auto runLambda = [&sendReceiverHandles](unsigned recv) {
                    // Runs the download thread as long as the group has work
                    sendReceiverHandles[recv].process(true);
                };
                asyncSendReceiverThreads[i] = async(launch::async, runLambda, i);
            }

            // No handle is stopped here, only a stuck daemon needs the escape hatch
            for (auto i = 0u; i < daemons; i++) {
                auto ended = asyncSendReceiverThreads[i].wait_for(requestLimit) == future_status::ready;
                CHECK(ended);
                if (!ended)
                    sendReceiverHandles[i].stop();
                asyncSendReceiverThreads[i].get();
            }

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
        // No daemon runs, so the controller measures an idle group
        auto recommendation = controller.current();
        auto start = chrono::steady_clock::now();
        while (recommendation.threads != 1 && chrono::steady_clock::now() - start < collapseLimit) {
            recommendation = controller.recommend(group);
            usleep(1000);
        }
        REQUIRE(recommendation.threads == 1);
        REQUIRE(recommendation.requestsPerThread >= 1);
    }

    SECTION("resizing a running pool answers every request") {
        auto runningDaemons = 0u;

        // Start and stop daemons that outlive their work until the pool matches the size
        auto reconcileDaemons = [&](unsigned daemons) {
            auto target = min(daemons, maxDaemons);
            while (runningDaemons < target) {
                auto runLambda = [&sendReceiverHandles](unsigned recv) {
                    // Runs the download thread in full daemon mode
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

        // Create the get request
        anyblob::network::Transaction getTxn(provider.get());
        createDownloads(getTxn, finishedMessages, validMessages);

        // Retrieve the request asynchronously
        REQUIRE(getTxn.processAsync(group));

        // Walk the pool and the per daemon requests through their bounds, shrinking drains a daemon
        unsigned daemonSteps[]{1, maxDaemons, 2, maxDaemons, 1};
        unsigned requestSteps[]{1, 32, 4, 16, 8};
        for (auto step = 0u; step < 10; step++) {
            group.setConcurrentRequests(min(requestSteps[step % 5], group.maxConcurrentRequests()));
            reconcileDaemons(daemonSteps[step % 5]);
        }

        // Wait for the download
        auto start = chrono::steady_clock::now();
        while (finishedMessages != requests && chrono::steady_clock::now() - start < requestLimit)
            usleep(100);

        // Stop the daemons before the check, no late callback may arrive
        reconcileDaemons(0);
        REQUIRE(finishedMessages == requests);
        REQUIRE(validMessages == requests);
        REQUIRE(group.getQueuedMessages() == 0);
        REQUIRE(group.getInflightMessages() == 0);
    }

    {
        // Create the delete request
        anyblob::network::Transaction deleteTxn(provider.get());
        for (auto i = 0u; i < 2; i++) {
            auto& currentFileName = fileName[i];
            auto deleteRequest = [&deleteTxn, &currentFileName]() {
                return deleteTxn.deleteObjectRequest(currentFileName);
            };
            deleteTxn.verifyKeyRequest(sendReceiverHandle, move(deleteRequest));
        }

        // Delete the request synchronously with the scheduler object on this thread
        deleteTxn.processSync(sendReceiverHandle);

        // Check the deletion
        for (const auto& it : deleteTxn) {
            // Sucessful request
            REQUIRE(it.success());
        }
    }

    REQUIRE(group.getInflightMessages() == 0);
}
//---------------------------------------------------------------------------
} // namespace anyblob::test
