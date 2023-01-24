#include "catch2/single_include/catch2/catch.hpp"
#include "cloud/aws.hpp"
#include "cloud/provider.hpp"
#include "network/tasked_send_receiver.hpp"
#include "network/transaction.hpp"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
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

    // Create the put request
    vector<unique_ptr<anyblob::network::Transaction>> requests;
    requests.emplace_back(make_unique<anyblob::network::PutTransaction>(fileName, content.data(), content.size()));

    // Upload the request synchronously with the scheduler object on this thread
    provider->uploadObjects(sendReceiver, requests);

    // Create the get request
    requests.clear();
    requests.emplace_back(make_unique<anyblob::network::GetTransaction>(fileName));

    // Retrieve the request synchronously with the scheduler object on this thread
    provider->retrieveObjects(sendReceiver, requests);

    // Get and print the result, note that the data lies in the data buffer but after the offset to skip the HTTP header
    // Note that the size is already without the header, so the full request has size + offset length
    auto& request = requests.front();
    string_view dataString(reinterpret_cast<char*>(request->data.get()) + request->offset, request->size);
    REQUIRE(!content.compare(dataString));
}
//---------------------------------------------------------------------------
} // namespace test
} // namespace anyblob
