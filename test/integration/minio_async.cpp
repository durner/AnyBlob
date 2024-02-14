#include "catch2/single_include/catch2/catch.hpp"
#include "cloud/minio.hpp"
#include "cloud/provider.hpp"
#include "network/tasked_send_receiver.hpp"
#include "network/transaction.hpp"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace test {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
TEST_CASE("MinIO Asynchronous Integration") {
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

    // The file to be uploaded and downloaded
    string bucketName = "minio://";
    bucketName = bucketName + endpoint + "/" + bucket + ":" + region;
    string fileName[]{"test.txt", "long.txt"};
    string longText = stringGen(1 << 24);
    string content[]{"Hello World!", longText};

    // Create a new task group
    anyblob::network::TaskedSendReceiverGroup group;

    // Create an AnyBlob scheduler object for the group
    anyblob::network::TaskedSendReceiver sendReceiver(group);

    // Async thread
    future<void> asyncSendReceiverThread;

    auto runLambda = [&]() {
        // Runs the download thread to asynchronously retrieve data
        sendReceiver.run();
    };
    asyncSendReceiverThread = async(launch::async, runLambda);

    // Create the provider for the corresponding filename
    auto provider = anyblob::cloud::Provider::makeProvider(bucketName, false, key, secret, &sendReceiver);

    // Update the concurrency according to instance settings (noop here, as minio uses default values)
    auto config = provider->getConfig(sendReceiver);
    group.setConfig(config);

    {
        // Check the upload for success
        atomic<uint16_t> finishedMessages = 0;
        auto checkSuccess = [&finishedMessages](anyblob::network::MessageResult& result) {
            // Sucessful request
            REQUIRE(result.success());
            finishedMessages++;
        };

        // Create the put request
        anyblob::network::Transaction putTxn(provider.get());
        for (auto i = 0u; i < 2; i++) {
            auto putObjectRequest = [&putTxn, &fileName, &content, &checkSuccess, i]() {
                return putTxn.putObjectRequest(checkSuccess, fileName[i], content[i].data(), content[i].size());
            };
            putTxn.verifyKeyRequest(sendReceiver, move(putObjectRequest));
        }

        // Upload the request asynchronously
        putTxn.processAsync(group);

        // Wait for the upload
        while (finishedMessages != 2)
            usleep(100);
    }
    {
        // Check the upload for success
        atomic<uint16_t> finishedMessages = 0;
        auto checkSuccess = [&finishedMessages](anyblob::network::MessageResult& result) {
            // Sucessful request
            REQUIRE(result.success());
            finishedMessages++;
        };

        // Create the multipart put request
        auto minio = static_cast<anyblob::cloud::MinIO*>(provider.get());
        minio->setMultipartUploadSize(6ull << 20);
        anyblob::network::Transaction putTxn(provider.get());
        for (auto i = 0u; i < 2; i++) {
            auto putObjectRequest = [&putTxn, &fileName, &content, &checkSuccess, i]() {
                return putTxn.putObjectRequest(checkSuccess, fileName[i], content[i].data(), content[i].size());
            };
            putTxn.verifyKeyRequest(sendReceiver, move(putObjectRequest));
        }

        while (finishedMessages != 2) {
            // Upload the new request asynchronously
            putTxn.processAsync(group);
            // Wait for the upload
            usleep(100);
        }
    }
    {
        // Check the upload for failure due to too small part
        atomic<uint16_t> finishedMessages = 0;
        auto checkSuccess = [&finishedMessages](anyblob::network::MessageResult& result) {
            // Sucessful request
            REQUIRE(!result.success());
            finishedMessages++;
        };

        // Create the multipart put request
        auto minio = static_cast<anyblob::cloud::MinIO*>(provider.get());
        minio->setMultipartUploadSize(1ull << 20); // too small, requires at least 5MiB parts
        anyblob::network::Transaction putTxn(provider.get());

        auto putObjectRequest = [&putTxn, &fileName, &content, callback = move(checkSuccess)]() {
            return putTxn.putObjectRequest(move(callback), fileName[1], content[1].data(), content[1].size());
        };
        putTxn.verifyKeyRequest(sendReceiver, move(putObjectRequest));

        while (finishedMessages != 1) {
            // Upload the new request asynchronously
            putTxn.processAsync(group);
            // Wait for the upload
            usleep(100);
        }
    }
    {
        atomic<uint16_t> finishedMessages = 0;
        // Create the get request
        anyblob::network::Transaction getTxn(provider.get());
        for (auto i = 0u; i < 2; i++) {
            // Check the download for success
            auto checkSuccess = [&finishedMessages, &content, i](anyblob::network::MessageResult& result) {
                // Sucessful request
                REQUIRE(result.success());
                // Simple string_view interface
                REQUIRE(!content[i].compare(result.getResult()));
                // Check from the other side too
                REQUIRE(!result.getResult().compare(content[i]));
                // Check the size
                REQUIRE(result.getSize() == content[i].size());

                // Advanced raw interface
                // Note that the data lies in the data buffer but after the offset to skip the HTTP header
                // Note that the size is already without the header, so the full request has size + offset length
                string_view rawDataString(reinterpret_cast<const char*>(result.getData()) + result.getOffset(), result.getSize());
                REQUIRE(!content[i].compare(rawDataString));
                REQUIRE(!rawDataString.compare(result.getResult()));
                REQUIRE(!rawDataString.compare(content[i]));
                finishedMessages++;
            };

            auto& currentFileName = fileName[i];
            auto getObjectRequest = [&getTxn, &currentFileName, callback = move(checkSuccess)]() {
                return getTxn.getObjectRequest(move(callback), currentFileName);
            };
            getTxn.verifyKeyRequest(sendReceiver, move(getObjectRequest));
        }

        // Retrieve the request asynchronously
        getTxn.processAsync(group);

        // Wait for the download
        while (finishedMessages != 2)
            usleep(100);
    }
    {
        // Check the delete for success
        atomic<uint16_t> finishedMessages = 0;
        auto checkSuccess = [&finishedMessages](anyblob::network::MessageResult& result) {
            // Sucessful request
            REQUIRE(result.success());
            finishedMessages++;
        };

        // Create the delete request
        anyblob::network::Transaction deleteTxn(provider.get());
        for (auto i = 0u; i < 2; i++) {
            auto& currentFileName = fileName[i];
            auto deleteRequest = [&deleteTxn, &currentFileName, callback = move(checkSuccess)]() {
                return deleteTxn.deleteObjectRequest(move(callback), currentFileName);
            };
            deleteTxn.verifyKeyRequest(sendReceiver, move(deleteRequest));
        }

        // Process the request asynchronously
        deleteTxn.processAsync(group);

        // Wait for the deletion
        while (finishedMessages != 2)
            usleep(100);
    }

    // Stop the send receiver daemon
    sendReceiver.stop();
    // Join the thread
    asyncSendReceiverThread.get();
}
//---------------------------------------------------------------------------
} // namespace test
} // namespace anyblob
