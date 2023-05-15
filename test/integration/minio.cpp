#include "catch2/single_include/catch2/catch.hpp"
#include "cloud/provider.hpp"
#include "network/get_transaction.hpp"
#include "network/put_transaction.hpp"
#include "network/tasked_send_receiver.hpp"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
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
TEST_CASE("MinIO Integration") {
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

    // The file to be uploaded and downloaded
    string bucketName = "minio://";
    bucketName = bucketName + endpoint + "/" + bucket + ":" + region;
    auto fileName = "test.txt";
    string content = "Hello World!";

    // Create a new task group (18 concurrent request maximum, and up to 1024 outstanding submissions)
    anyblob::network::TaskedSendReceiverGroup group(18, 1024);

    // Create an AnyBlob scheduler object for the group
    anyblob::network::TaskedSendReceiver sendReceiver(group);

    // Create the provider for the corresponding filename
    auto provider = anyblob::cloud::Provider::makeProvider(bucketName, key, secret, &sendReceiver);
    {
        // Create the put request
        anyblob::network::PutTransaction putTxn(provider.get());
        putTxn.addRequest(fileName, content.data(), content.size());

        // Upload the request synchronously with the scheduler object on this thread
        putTxn.processSync(sendReceiver);

        // Check the upload
        for (const auto& it : putTxn) {
            // Sucessful request
            REQUIRE(it.success());
        }
    }
    {
        // Create the get request
        anyblob::network::GetTransaction getTxn(provider.get());
        getTxn.addRequest(fileName);

        // Retrieve the request synchronously with the scheduler object on this thread
        getTxn.processSync(sendReceiver);

        // Get and print the result
        for (const auto& it : getTxn) {
            // Sucessful request
            REQUIRE(it.success());
            // Simple string_view interface
            REQUIRE(!content.compare(it.getResult()));

            // Advanced raw interface
            // Note that the data lies in the data buffer but after the offset to skip the HTTP header
            // Note that the size is already without the header, so the full request has size + offset length
            string_view rawDataString(reinterpret_cast<const char*>(it.getData()) + it.getOffset(), it.getSize());
            REQUIRE(!content.compare(rawDataString));
            REQUIRE(!rawDataString.compare(it.getResult()));
        }
    }
}
//---------------------------------------------------------------------------
} // namespace test
} // namespace anyblob
