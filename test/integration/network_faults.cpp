#include "catch2/single_include/catch2/catch.hpp"
#include "cloud/provider.hpp"
#include "fault_proxy.hpp"
#include "network/tasked_send_receiver.hpp"
#include "network/transaction.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <string>
#include <thread>
#include <unistd.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2026
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
//---------------------------------------------------------------------------
using namespace std;
using namespace std::chrono_literals;
//---------------------------------------------------------------------------
namespace {
//---------------------------------------------------------------------------
using FaultProxy = test::FaultProxy;
using Mode = FaultProxy::Mode;
//---------------------------------------------------------------------------
/// The environment of the integration setup
struct Environment {
    string bucket, region, endpoint, key, secret;

    static Environment get() {
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
        return {bucket, region, endpoint, key, secret};
    }

    /// Provider uri through the given endpoint
    [[nodiscard]] string uri(const string& viaEndpoint) const {
        return "minio://" + viaEndpoint + "/" + bucket + ":" + region;
    }
};
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
/// Deterministic test content
string testContent() {
    string content(4u << 20, '\0');
    for (auto i = 0u; i < content.size(); i++)
        content[i] = static_cast<char>('A' + (i % 53));
    return content;
}
//---------------------------------------------------------------------------
/// Open file descriptor count of this process
size_t fdCount() {
    return static_cast<size_t>(distance(filesystem::directory_iterator("/proc/self/fd"), filesystem::directory_iterator{}));
}
//---------------------------------------------------------------------------
/// Use short timeouts for fault tests
unique_ptr<network::TaskedSendReceiverGroup> makeGroup() {
    auto group = make_unique<network::TaskedSendReceiverGroup>();
    group->getTCPSettings().timeout = 250ms;
    group->getTCPSettings().requestDeadline = 5s;
    return group;
}
//---------------------------------------------------------------------------
/// Check GET completion time and optional content
bool boundedGet(cloud::Provider& provider, network::TaskedSendReceiverHandle& handle, const string& file, const string* expected) {
    network::Transaction txn(&provider);
    txn.verifyKeyRequest(handle, [&]() { return txn.getObjectRequest(file); });
    auto start = chrono::steady_clock::now();
    txn.processSync(handle);
    CHECK(chrono::steady_clock::now() - start < 15s);
    for (const auto& it : txn) {
        if (!it.success())
            return false;
        if (expected) {
            CHECK(it.getSize() == expected->size());
            CHECK(!expected->compare(it.getResult()));
        }
    }
    return true;
}
//---------------------------------------------------------------------------
/// Runs one synchronous put, bounded
bool boundedPut(cloud::Provider& provider, network::TaskedSendReceiverHandle& handle, const string& file, const string& content) {
    network::Transaction txn(&provider);
    txn.verifyKeyRequest(handle, [&]() { return txn.putObjectRequest(file, content.data(), content.size()); });
    auto start = chrono::steady_clock::now();
    txn.processSync(handle);
    CHECK(chrono::steady_clock::now() - start < 15s);
    for (const auto& it : txn)
        if (!it.success())
            return false;
    return true;
}
//---------------------------------------------------------------------------
} // namespace
//---------------------------------------------------------------------------
TEST_CASE("Network Fault Integration") {
    auto env = Environment::get();
    auto content = testContent();
    auto fds = fdCount();

    // Seed the test object through the direct endpoint
    {
        auto group = makeGroup();
        auto handle = group->getHandle();
        auto provider = cloud::Provider::makeProvider(env.uri(env.endpoint), false, env.key, env.secret, &handle);
        CHECK(boundedPut(*provider, handle, "faults/data.bin", content));
    }

    SECTION("transient faults recover") {
        // The cut is tested at a header and at a body offset
        for (auto& [mode, arg] : {pair<Mode, uint64_t>{Mode::rstOnConnect, 0}, {Mode::closeMidBody, 64 << 10}, {Mode::rstMidBody, 64 << 10}, {Mode::closeMidBody, 1 << 20}, {Mode::garbage, 0}}) {
            FaultProxy proxy(env.endpoint, mode, arg, 2);
            auto group = makeGroup();
            auto handle = group->getHandle();
            auto provider = cloud::Provider::makeProvider(env.uri(proxy.getEndpoint()), false, env.key, env.secret, &handle);
            INFO("mode " << FaultProxy::getName(mode) << " arg " << arg);
            CHECK(boundedGet(*provider, handle, "faults/data.bin", &content));
            // Two faulted attempts plus the successful one
            CHECK(proxy.getAccepted() == 3);
            CHECK(group->getInflightMessages() == 0);
        }
    }

    SECTION("persistent faults abort within the budget") {
        for (auto& [mode, arg] : {pair<Mode, uint64_t>{Mode::rstOnConnect, 0}, {Mode::acceptHang, 0}, {Mode::headerStall, 1024}, {Mode::garbage, 0}}) {
            FaultProxy proxy(env.endpoint, mode, arg, -1);
            auto group = makeGroup();
            auto handle = group->getHandle();
            auto provider = cloud::Provider::makeProvider(env.uri(proxy.getEndpoint()), false, env.key, env.secret, &handle);
            INFO("mode " << FaultProxy::getName(mode) << " arg " << arg);
            CHECK(!boundedGet(*provider, handle, "faults/data.bin", nullptr));
            // Giving up must follow retries
            CHECK(proxy.getAccepted() > 1);
            CHECK(group->getInflightMessages() == 0);
        }
    }

    SECTION("slow progress cannot outrun the deadline") {
        // Progress prevents timeouts; the deadline must stop the transfer
        FaultProxy proxy(env.endpoint, Mode::trickle, 200000, -1);
        auto group = makeGroup();
        auto handle = group->getHandle();
        auto provider = cloud::Provider::makeProvider(env.uri(proxy.getEndpoint()), false, env.key, env.secret, &handle);
        auto start = chrono::steady_clock::now();
        CHECK(!boundedGet(*provider, handle, "faults/data.bin", nullptr));
        // Completion would take about 20 seconds
        CHECK(chrono::steady_clock::now() - start < 15s);
        CHECK(group->getInflightMessages() == 0);
    }

    SECTION("slow transfers within the deadline still succeed") {
        FaultProxy proxy(env.endpoint, Mode::trickle, 4000000, -1);
        auto group = makeGroup();
        auto handle = group->getHandle();
        auto provider = cloud::Provider::makeProvider(env.uri(proxy.getEndpoint()), false, env.key, env.secret, &handle);
        CHECK(boundedGet(*provider, handle, "faults/data.bin", &content));
    }

    SECTION("progress restores the per-attempt timeout") {
        // Each failed attempt delivers a partial body
        FaultProxy proxy(env.endpoint, Mode::headerStall, 256 << 10, 7);
        auto group = makeGroup();
        auto handle = group->getHandle();
        auto provider = cloud::Provider::makeProvider(env.uri(proxy.getEndpoint()), false, env.key, env.secret, &handle);
        auto start = chrono::steady_clock::now();
        CHECK(boundedGet(*provider, handle, "faults/data.bin", &content));
        CHECK(chrono::steady_clock::now() - start < 5s);
        CHECK(group->getInflightMessages() == 0);
    }

    SECTION("small response delays succeed") {
        FaultProxy proxy(env.endpoint, Mode::responseDelay, 40, -1);
        auto group = makeGroup();
        auto handle = group->getHandle();
        auto provider = cloud::Provider::makeProvider(env.uri(proxy.getEndpoint()), false, env.key, env.secret, &handle);
        CHECK(boundedGet(*provider, handle, "faults/data.bin", &content));
    }

    SECTION("upload resets must not kill or stall the process") {
        // Transient reset while sending: retries finish the upload
        {
            FaultProxy proxy(env.endpoint, Mode::rstMidUpload, 256 << 10, 2);
            auto group = makeGroup();
            auto handle = group->getHandle();
            auto provider = cloud::Provider::makeProvider(env.uri(proxy.getEndpoint()), false, env.key, env.secret, &handle);
            CHECK(boundedPut(*provider, handle, "faults/upload.bin", content));
        }
        // Persistent reset aborts within the budget
        {
            FaultProxy proxy(env.endpoint, Mode::rstMidUpload, 256 << 10, -1);
            auto group = makeGroup();
            auto handle = group->getHandle();
            auto provider = cloud::Provider::makeProvider(env.uri(proxy.getEndpoint()), false, env.key, env.secret, &handle);
            CHECK(!boundedPut(*provider, handle, "faults/upload.bin", content));
            CHECK(group->getInflightMessages() == 0);
        }
    }

    SECTION("idle closed connections are not reused") {
        FaultProxy proxy(env.endpoint, Mode::idleClose, 300, -1);
        auto group = makeGroup();
        auto handle = group->getHandle();
        auto provider = cloud::Provider::makeProvider(env.uri(proxy.getEndpoint()), false, env.key, env.secret, &handle);
        CHECK(boundedGet(*provider, handle, "faults/data.bin", &content));
        CHECK(proxy.getAccepted() == 1);
        // Let the proxy close the idle connection
        this_thread::sleep_for(1s);
        CHECK(boundedGet(*provider, handle, "faults/data.bin", &content));
        CHECK(proxy.getAccepted() == 2);
    }

    SECTION("async storm through transient faults") {
        FaultProxy proxy(env.endpoint, Mode::rstMidBody, 64 << 10, 8);
        auto group = makeGroup();
        auto handle = group->getHandle();
        auto provider = cloud::Provider::makeProvider(env.uri(proxy.getEndpoint()), false, env.key, env.secret, &handle);

        auto worker = async(launch::async, [&]() { handle.process(false); });
        atomic<unsigned> finished = 0;
        network::Transaction txn(provider.get());
        auto callback = [&finished, &content](network::MessageResult& result) {
            CHECK(result.success());
            CHECK(result.getSize() == content.size());
            finished++;
        };
        txn.verifyKeyRequest(handle, [&]() {
            for (auto i = 0u; i < 16; i++)
                if (!txn.getObjectRequest(callback, "faults/data.bin"))
                    return false;
            return true;
        });
        CHECK(txn.processAsync(*group));
        auto start = chrono::steady_clock::now();
        while (finished != 16 && chrono::steady_clock::now() - start < 30s)
            this_thread::sleep_for(1ms);
        CHECK(finished == 16);
        handle.stop();
        worker.get();
        CHECK(group->getInflightMessages() == 0);
    }

    SECTION("hive style keys round trip") {
        auto group = makeGroup();
        auto handle = group->getHandle();
        auto provider = cloud::Provider::makeProvider(env.uri(env.endpoint), false, env.key, env.secret, &handle);
        string hive = "faults/dt=2024-01-01/part=0/x.bin";
        CHECK(boundedPut(*provider, handle, hive, content));
        CHECK(boundedGet(*provider, handle, hive, &content));
    }

    // No socket leaks across the scenarios
    CHECK(fdCount() <= fds + 8);
}
//---------------------------------------------------------------------------
} // namespace anyblob
