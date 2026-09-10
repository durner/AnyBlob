#include "catch2/single_include/catch2/catch.hpp"
#include "cloud/provider.hpp"
#include "network/tasked_send_receiver.hpp"
#include "network/transaction.hpp"
#include <cstring>
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
// Test the public ClickBench dataset
TEST_CASE("Anonymous Parquet") {
    // The footer of a parquet file is its last four bytes of magic behind its length
    static constexpr uint64_t footerTail = 8;
    string bucketName = "s3://clickhouse-public-datasets:eu-central-1/";
    string partition = "hits_compatible/athena_partitioned/hits_0.parquet";
    string wholeDataset = "hits_compatible/hits.parquet";

    // Create a new task group
    anyblob::network::TaskedSendReceiverGroup group;

    // Create an AnyBlob scheduler object for the group
    auto sendReceiverHandle = group.getHandle();

    // A public bucket is addressed without credentials, but still over a verified connection
    auto provider = anyblob::cloud::Provider::makeAnonymousProvider(bucketName, true);
    REQUIRE(provider->useTls());
    REQUIRE(provider->verifyPeer());

    // Update the concurrency according to instance settings, the lookup fails fast off an instance
    auto config = provider->getConfig(sendReceiverHandle);
    group.setConfig(config);
    REQUIRE(group.getConcurrentRequests() == anyblob::network::Config::defaultCoreConcurrency);

    // The tail of the file reports the footer length and the size of the whole object
    uint32_t footerLength = 0;
    uint64_t objectSize = 0;
    {
        anyblob::network::Transaction txn(provider.get());
        REQUIRE(txn.getObjectSuffixRequest(partition, footerTail));
        txn.processSync(sendReceiverHandle);
        for (const auto& it : txn) {
            REQUIRE(it.success());
            REQUIRE(it.getSize() == footerTail);
            auto tail = it.getResult();
            REQUIRE(!tail.substr(4).compare("PAR1"));
            memcpy(&footerLength, tail.data(), sizeof(footerLength));
            objectSize = it.getObjectSize();
            REQUIRE(objectSize > footerLength + footerTail);
        }
    }

    // The footer itself is addressed by the size that came back with the tail
    {
        auto footerStart = objectSize - footerLength - footerTail;
        anyblob::network::Transaction txn(provider.get());
        REQUIRE(txn.getObjectRequest(partition, {footerStart, footerStart + footerLength}));
        txn.processSync(sendReceiverHandle);
        for (const auto& it : txn) {
            REQUIRE(it.success());
            REQUIRE(it.getSize() == footerLength);
            REQUIRE(it.getObjectSize() == objectSize);
        }
    }

    // The size of the whole dataset costs the same eight bytes
    {
        REQUIRE(anyblob::cloud::Provider::getObjectKey(bucketName + wholeDataset) == wholeDataset);
        anyblob::network::Transaction txn(provider.get());
        REQUIRE(txn.getObjectSuffixRequest(wholeDataset, footerTail));
        txn.processSync(sendReceiverHandle);
        for (const auto& it : txn) {
            REQUIRE(it.success());
            REQUIRE(!it.getResult().substr(4).compare("PAR1"));
            REQUIRE(it.getObjectSize() > (13ull << 30));
        }
    }

    // The partitions of the dataset are listed without credentials as well
    {
        anyblob::network::Transaction txn(provider.get());
        REQUIRE(txn.listObjectsRequest("hits_compatible/athena_partitioned/", {}, 4));
        txn.processSync(sendReceiverHandle);
        string continuationToken;
        for (const auto& it : txn) {
            REQUIRE(it.success());
            auto keys = provider->getListObjectKeys(it.getResult(), continuationToken);
            REQUIRE(keys.size() == 4);
            REQUIRE(keys[0] == partition);
            // The dataset holds more partitions than the request asked for
            REQUIRE(!continuationToken.empty());
        }
    }
}
//---------------------------------------------------------------------------
} // namespace anyblob::test
