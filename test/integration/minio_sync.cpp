#include "catch2/single_include/catch2/catch.hpp"
#include "cloud/minio.hpp"
#include "cloud/provider.hpp"
#include "network/tasked_send_receiver.hpp"
#include "network/transaction.hpp"
#include <cstdlib>
#include <cstring>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::test {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
TEST_CASE("MinIO Sync Integration") {
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
    auto sendReceiverHandle = group.getHandle();

    // Create the provider for the corresponding filename
    auto provider = anyblob::cloud::Provider::makeProvider(bucketName, false, key, secret, &sendReceiverHandle);

    // Update the concurrency according to instance settings (noop here, as minio uses default values)
    auto config = provider->getConfig(sendReceiverHandle);
    group.setConfig(config);

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
    {
        // Create the multipart put request
        auto minio = static_cast<anyblob::cloud::MinIO*>(provider.get());
        minio->setMultipartUploadSize(6ull << 20);
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
    {
        // Create the multipart put request but enforce failure due to small part size
        auto minio = static_cast<anyblob::cloud::MinIO*>(provider.get());
        minio->setMultipartUploadSize(1ull << 20); // part size must be larger than 5MiB
        anyblob::network::Transaction putTxn(provider.get());
        auto putObjectRequest = [&putTxn, &fileName, &content]() {
            return putTxn.putObjectRequest(fileName[1], content[1].data(), content[1].size());
        };
        putTxn.verifyKeyRequest(sendReceiverHandle, move(putObjectRequest));

        // Upload the request synchronously with the scheduler object on this thread
        putTxn.processSync(sendReceiverHandle);

        // Check the upload
        for (const auto& it : putTxn) {
            // Sucessful request
            REQUIRE(!it.success());
        }
    }
    {
        // Create the get request
        anyblob::network::Transaction getTxn(provider.get());
        for (auto i = 0u; i < 2; i++) {
            auto& currentFileName = fileName[i];
            auto getObjectRequest = [&getTxn, &currentFileName]() {
                return getTxn.getObjectRequest(currentFileName);
            };
            getTxn.verifyKeyRequest(sendReceiverHandle, move(getObjectRequest));
        }

        // Retrieve the request synchronously with the scheduler object on this thread
        getTxn.processSync(sendReceiverHandle);

        // Get and print the result
        auto i = 0u;
        for (const auto& it : getTxn) {
            // Sucessful request
            REQUIRE(it.success());
            // Simple string_view interface
            REQUIRE(!content[i].compare(it.getResult()));
            // Check from the other side too
            REQUIRE(!it.getResult().compare(content[i]));
            // Check the size
            REQUIRE(it.getSize() == content[i].size());
            // A whole object reports its size as well
            REQUIRE(it.getObjectSize() == content[i].size());

            // Advanced raw interface
            // Note that the data lies in the data buffer but after the offset to skip the HTTP header
            // Note that the size is already without the header, so the full request has size + offset length
            string_view rawDataString(reinterpret_cast<const char*>(it.getData()) + it.getOffset(), it.getSize());
            REQUIRE(!content[i].compare(rawDataString));
            REQUIRE(!rawDataString.compare(it.getResult()));
            REQUIRE(!rawDataString.compare(content[i++]));
        }
    }
    {
        // A ranged get reports the size of the whole object
        anyblob::network::Transaction rangeTxn(provider.get());
        auto rangeRequest = [&rangeTxn, &fileName]() {
            return rangeTxn.getObjectRequest(fileName[1], {8, 24});
        };
        rangeTxn.verifyKeyRequest(sendReceiverHandle, move(rangeRequest));
        rangeTxn.processSync(sendReceiverHandle);
        for (const auto& it : rangeTxn) {
            REQUIRE(it.success());
            REQUIRE(it.getSize() == 16);
            REQUIRE(it.getObjectSize() == content[1].size());
            REQUIRE(!it.getResult().compare(string_view(content[1]).substr(8, 16)));
        }
    }
    {
        // Exercise transfers with a short timeout
        auto storedTimeout = group.getTCPSettings().timeout;
        group.getTCPSettings().timeout = std::chrono::milliseconds(20);
        anyblob::network::Transaction stormTxn(provider.get());
        auto stormRequest = [&stormTxn, &fileName]() {
            return stormTxn.getObjectRequest(fileName[1]);
        };
        stormTxn.verifyKeyRequest(sendReceiverHandle, move(stormRequest));
        stormTxn.processSync(sendReceiverHandle);
        for (const auto& it : stormTxn) {
            REQUIRE(it.success());
            REQUIRE(it.getSize() == content[1].size());
        }
        group.getTCPSettings().timeout = storedTimeout;
    }
    {
        // The footer of an object is read without asking for its size first
        anyblob::network::Transaction suffixTxn(provider.get());
        auto suffixRequest = [&suffixTxn, &fileName]() {
            return suffixTxn.getObjectSuffixRequest(fileName[1], 64);
        };
        suffixTxn.verifyKeyRequest(sendReceiverHandle, move(suffixRequest));
        suffixTxn.processSync(sendReceiverHandle);
        for (const auto& it : suffixTxn) {
            REQUIRE(it.success());
            REQUIRE(it.getSize() == 64);
            REQUIRE(it.getObjectSize() == content[1].size());
            REQUIRE(!it.getResult().compare(string_view(content[1]).substr(content[1].size() - 64)));
        }
    }
    {
        // List what the bucket holds under a prefix
        string listPrefix = "list/";
        string listNames[]{listPrefix + "a.parquet", listPrefix + "b.parquet"};
        anyblob::network::Transaction listPutTxn(provider.get());
        for (auto i = 0u; i < 2; i++) {
            auto& currentFileName = listNames[i];
            auto putObjectRequest = [&listPutTxn, &currentFileName, &content]() {
                return listPutTxn.putObjectRequest(currentFileName, content[0].data(), content[0].size());
            };
            listPutTxn.verifyKeyRequest(sendReceiverHandle, move(putObjectRequest));
        }
        listPutTxn.processSync(sendReceiverHandle);
        for (const auto& it : listPutTxn)
            REQUIRE(it.success());

        anyblob::network::Transaction listTxn(provider.get());
        auto listObjectsRequest = [&listTxn, &listPrefix]() {
            return listTxn.listObjectsRequest(listPrefix);
        };
        listTxn.verifyKeyRequest(sendReceiverHandle, move(listObjectsRequest));
        listTxn.processSync(sendReceiverHandle);
        string continuationToken = "unset";
        for (const auto& it : listTxn) {
            REQUIRE(it.success());
            auto keys = provider->getListObjectKeys(it.getResult(), continuationToken);
            REQUIRE(keys.size() == 2);
            REQUIRE(keys[0] == listNames[0]);
            REQUIRE(keys[1] == listNames[1]);
            REQUIRE(continuationToken.empty());
        }

        // A truncated result is continued with the token it came back with
        vector<string> pagedKeys;
        auto pages = 0u;
        do {
            anyblob::network::Transaction pageTxn(provider.get());
            auto pageRequest = [&pageTxn, &listPrefix, &continuationToken]() {
                return pageTxn.listObjectsRequest(listPrefix, continuationToken, 1);
            };
            pageTxn.verifyKeyRequest(sendReceiverHandle, move(pageRequest));
            pageTxn.processSync(sendReceiverHandle);
            for (const auto& it : pageTxn) {
                REQUIRE(it.success());
                auto keys = provider->getListObjectKeys(it.getResult(), continuationToken);
                REQUIRE(keys.size() == 1);
                pagedKeys.push_back(keys[0]);
            }
        } while (!continuationToken.empty() && ++pages < 8);
        REQUIRE(pagedKeys.size() == 2);
        REQUIRE(pagedKeys[0] == listNames[0]);
        REQUIRE(pagedKeys[1] == listNames[1]);

        anyblob::network::Transaction listDeleteTxn(provider.get());
        for (auto i = 0u; i < 2; i++) {
            auto& currentFileName = listNames[i];
            auto deleteObjectRequest = [&listDeleteTxn, &currentFileName]() {
                return listDeleteTxn.deleteObjectRequest(currentFileName);
            };
            listDeleteTxn.verifyKeyRequest(sendReceiverHandle, move(deleteObjectRequest));
        }
        listDeleteTxn.processSync(sendReceiverHandle);
        for (const auto& it : listDeleteTxn)
            REQUIRE(it.success());
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

        // Process the request synchronously with the scheduler object on this thread
        deleteTxn.processSync(sendReceiverHandle);

        // Check the deletion
        for (const auto& it : deleteTxn) {
            // Sucessful request
            REQUIRE(it.success());
        }
    }
}
//---------------------------------------------------------------------------
} // namespace anyblob::test
