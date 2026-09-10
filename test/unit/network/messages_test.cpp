#include "catch2/single_include/catch2/catch.hpp"
#include "cloud/provider.hpp"
#include "network/connection_manager.hpp"
#include "network/http_message.hpp"
#include "network/message_result.hpp"
#include "network/original_message.hpp"
#include "network/transaction.hpp"
#include "utils/data_vector.hpp"
#include <chrono>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network::test {
//---------------------------------------------------------------------------
using namespace std;
using namespace std::chrono_literals;
//---------------------------------------------------------------------------
/// Provider stub for resigns
struct FakeProvider : cloud::Provider {
    /// The resign result
    utils::DataVector<uint8_t>* resignResult = nullptr;
    /// The path of the last request
    mutable string lastPath;
    /// The range of the last request
    mutable pair<uint64_t, uint64_t> lastRange = {};

    FakeProvider() { _type = CloudService::HTTP; }
    unique_ptr<utils::DataVector<uint8_t>> getRequest(const string& filePath, const pair<uint64_t, uint64_t>& range) const override {
        lastPath = filePath;
        lastRange = range;
        return make_unique<utils::DataVector<uint8_t>>();
    }
    unique_ptr<utils::DataVector<uint8_t>> putRequest(const string&, string_view) const override { return nullptr; }
    unique_ptr<utils::DataVector<uint8_t>> deleteRequest(const string&) const override { return nullptr; }
    string getAddress() const override { return "localhost"; }
    uint32_t getPort() const override { return 80; }
    Instance getInstanceDetails(TaskedSendReceiverHandle&) override { return {}; }
    bool supportsResigning() const override { return true; }
    unique_ptr<utils::DataVector<uint8_t>> resignRequest(const utils::DataVector<uint8_t>&, const uint8_t*, uint64_t) const override {
        return resignResult ? make_unique<utils::DataVector<uint8_t>>(*resignResult) : nullptr;
    }
};
//---------------------------------------------------------------------------
TEST_CASE("messages") {
    ConnectionManager::TCPSettings tcpSettings;
    FakeProvider provider;
    OriginalMessage message(make_unique<utils::DataVector<uint8_t>>(), provider);
    HTTPMessage task(&message, tcpSettings, 64u * 1024);

    // The per-attempt timeout increases exponentially while the connection stalls
    REQUIRE(task.attemptTimeout() == 500ms);
    task.stalls = 1;
    REQUIRE(task.attemptTimeout() == 1000ms);
    task.stalls = 4;
    REQUIRE(task.attemptTimeout() == 8000ms);
    task.stalls = 31;
    REQUIRE(task.attemptTimeout() == 8000ms);

    // Progress resets backoff, preserving the failure count
    task.failures = 7;
    task.progressed();
    REQUIRE(task.attemptTimeout() == 500ms);
    REQUIRE(task.failures == 7);
    task.failures = 0;

    // A retry within the budget
    ConnectionManager connectionManager(8);
    utils::DataVector<uint8_t> marker;
    marker.resize(4);
    provider.resignResult = &marker;
    task.reset(connectionManager, false);
    REQUIRE(message.result.getState() == MessageState::Init);
    REQUIRE(message.message->size() == marker.size());

    // A failed resign aborts
    provider.resignResult = nullptr;
    task.reset(connectionManager, false);
    REQUIRE(message.result.getState() == MessageState::Aborted);
    REQUIRE((message.result.getFailureCode() & static_cast<uint16_t>(MessageFailureCode::Resign)));

    // An exhausted budget aborts the retry
    OriginalMessage budgetMessage(make_unique<utils::DataVector<uint8_t>>(), provider);
    HTTPMessage budgetTask(&budgetMessage, tcpSettings, 64u * 1024);
    provider.resignResult = &marker;
    budgetTask.startTime -= 31s;
    budgetTask.reset(connectionManager, false);
    REQUIRE(budgetMessage.result.getState() == MessageState::Aborted);
    REQUIRE((budgetMessage.result.getFailureCode() & static_cast<uint16_t>(MessageFailureCode::Timeout)));

    // The deadline also applies without failures
    OriginalMessage deadlineMessage(make_unique<utils::DataVector<uint8_t>>(), provider);
    HTTPMessage deadlineTask(&deadlineMessage, tcpSettings, 64u * 1024);
    REQUIRE(!deadlineTask.expired(connectionManager));
    deadlineTask.startTime -= 29s;
    REQUIRE(!deadlineTask.expired(connectionManager));
    deadlineTask.startTime -= 2s;
    REQUIRE(deadlineTask.expired(connectionManager));

    // Large responses extend the deadline using measured throughput
    connectionManager.recordThroughput(1ull << 20, 1s);
    OriginalMessage scaledMessage(make_unique<utils::DataVector<uint8_t>>(), provider);
    HTTPMessage scaledTask(&scaledMessage, tcpSettings, 64u * 1024);
    scaledTask.info = make_unique<HttpHelper::Info>();
    // 100 MiB at 1 MiB/s predicts 100 seconds
    scaledTask.info->length = 100ull << 20;
    scaledTask.startTime -= 31s;
    REQUIRE(!scaledTask.expired(connectionManager));
    scaledTask.startTime -= 500s;
    REQUIRE(scaledTask.expired(connectionManager));

    // A zero budget means unlimited retries
    OriginalMessage unlimitedMessage(make_unique<utils::DataVector<uint8_t>>(), provider);
    HTTPMessage unlimitedTask(&unlimitedMessage, tcpSettings, 64u * 1024);
    tcpSettings.requestDeadline = 0ms;
    unlimitedTask.startTime -= 31s;
    unlimitedTask.reset(connectionManager, false);
    REQUIRE(unlimitedMessage.result.getState() == MessageState::Init);
}
//---------------------------------------------------------------------------
TEST_CASE("transaction_overloads") {
    FakeProvider provider;
    Transaction txn(&provider);
    string remotePath = "dir/file.parquet";

    // A braced range must not be taken for a callback
    REQUIRE(txn.getObjectRequest(remotePath, {8, 23}));
    REQUIRE(provider.lastPath == remotePath);
    REQUIRE(provider.lastRange == pair<uint64_t, uint64_t>(8, 23));

    auto callback = [](MessageResult&) {};
    REQUIRE(txn.getObjectRequest(callback, remotePath, {16, 32}));
    REQUIRE(provider.lastPath == remotePath);
    REQUIRE(provider.lastRange == pair<uint64_t, uint64_t>(16, 32));
}
//---------------------------------------------------------------------------
} // namespace anyblob::network::test
