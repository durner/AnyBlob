#include "catch2/single_include/catch2/catch.hpp"
#include "cloud/minio.hpp"
#include "cloud/provider.hpp"
#include "fault_proxy.hpp"
#include "network/tasked_send_receiver.hpp"
#include "network/transaction.hpp"
#include "tls_proxy.hpp"
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// CedarDB (Dominik Durner), 2026
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
/// A MinIO provider that speaks TLS to the proxy
class TlsMinIO : public cloud::MinIO {
    public:
    /// The constructor
    TlsMinIO(const RemoteInfo& info, const string& keyId, const string& key) : MinIO(info, keyId, key) {}
    /// Does the endpoint expect a TLS-encrypted connection
    [[nodiscard]] bool useTls() const override { return true; }
};
//---------------------------------------------------------------------------
/// Deterministic test content
string testContent() {
    string content(4u << 20, '\0');
    for (auto i = 0u; i < content.size(); i++)
        content[i] = static_cast<char>('A' + (i % 53));
    return content;
}
//---------------------------------------------------------------------------
/// Use short timeouts so a stalled TLS connection is not waited out
unique_ptr<network::TaskedSendReceiverGroup> makeGroup() {
    auto group = make_unique<network::TaskedSendReceiverGroup>();
    group->getTCPSettings().timeout = 250ms;
    group->getTCPSettings().requestDeadline = 10s;
    return group;
}
//---------------------------------------------------------------------------
} // namespace
//---------------------------------------------------------------------------
TEST_CASE("MinIO TLS Integration") {
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

    test::TlsProxy proxy(endpoint);
    auto makeProvider = [&](const string& viaEndpoint) {
        auto uri = "minio://" + viaEndpoint + "/" + bucket + ":" + region;
        return make_unique<TlsMinIO>(cloud::Provider::getRemoteInfo(uri), key, secret);
    };
    auto provider = makeProvider(proxy.getEndpoint());

    auto content = testContent();
    auto fileName = "tls/data.bin"s;

    auto group = makeGroup();
    auto handle = group->getHandle();

    {
        network::Transaction putTxn(provider.get());
        putTxn.verifyKeyRequest(handle, [&]() { return putTxn.putObjectRequest(fileName, content.data(), content.size()); });
        putTxn.processSync(handle);
        for (const auto& it : putTxn)
            REQUIRE(it.success());
    }

    SECTION("the tls round trip carries the whole object") {
        // The second round reuses the connection and its SSL state
        for (auto round = 0u; round != 2u; round++) {
            network::Transaction getTxn(provider.get());
            getTxn.verifyKeyRequest(handle, [&]() { return getTxn.getObjectRequest(fileName); });
            getTxn.processSync(handle);
            for (const auto& it : getTxn) {
                REQUIRE(it.success());
                CHECK(it.getSize() == content.size());
                CHECK(!content.compare(it.getResult()));
            }
        }
        // A new connection per request would accept three times
        CHECK(proxy.getAccepted() < 3);
    }

    SECTION("a missing object fails without a retry storm") {
        network::Transaction getTxn(provider.get());
        getTxn.verifyKeyRequest(handle, [&]() { return getTxn.getObjectRequest("tls/absent.bin"); });
        getTxn.processSync(handle);
        for (const auto& it : getTxn)
            CHECK(!it.success());
    }

    SECTION("a cut connection is retried with a new handshake") {
        // The fault hits the plaintext below the TLS termination
        test::FaultProxy faults(endpoint, test::FaultProxy::Mode::closeMidBody, 64 << 10, 2);
        test::TlsProxy faultyTls(faults.getEndpoint());
        auto faultyProvider = makeProvider(faultyTls.getEndpoint());
        network::Transaction getTxn(faultyProvider.get());
        getTxn.verifyKeyRequest(handle, [&]() { return getTxn.getObjectRequest(fileName); });
        getTxn.processSync(handle);
        for (const auto& it : getTxn) {
            REQUIRE(it.success());
            CHECK(!content.compare(it.getResult()));
        }
        // Two faulted attempts plus the successful one
        CHECK(faults.getAccepted() == 3);
    }

    SECTION("a large object survives the record path in small chunks") {
        auto smallChunks = make_unique<network::TaskedSendReceiverGroup>(4u << 10);
        smallChunks->getTCPSettings().timeout = 1s;
        smallChunks->getTCPSettings().requestDeadline = 60s;
        auto chunkHandle = smallChunks->getHandle();

        string large(8u << 20, '\0');
        for (auto i = 0u; i < large.size(); i++)
            large[i] = static_cast<char>('A' + (i % 251));

        network::Transaction putTxn(provider.get());
        putTxn.verifyKeyRequest(chunkHandle, [&]() { return putTxn.putObjectRequest("tls/large.bin", large.data(), large.size()); });
        putTxn.processSync(chunkHandle);
        for (const auto& it : putTxn)
            REQUIRE(it.success());

        network::Transaction getTxn(provider.get());
        getTxn.verifyKeyRequest(chunkHandle, [&]() { return getTxn.getObjectRequest("tls/large.bin"); });
        getTxn.processSync(chunkHandle);
        for (const auto& it : getTxn) {
            REQUIRE(it.success());
            CHECK(it.getSize() == large.size());
            CHECK(!large.compare(it.getResult()));
        }
        CHECK(smallChunks->getInflightMessages() == 0);
    }

    SECTION("a corrupted record fails the request instead of delivering it") {
        test::TlsProxy innerTls(endpoint);
        test::FaultProxy corrupt(innerTls.getEndpoint(), test::FaultProxy::Mode::corruptStream, 4 << 10, -1);
        auto corruptProvider = makeProvider(corrupt.getEndpoint());

        network::Transaction getTxn(corruptProvider.get());
        getTxn.verifyKeyRequest(handle, [&]() { return getTxn.getObjectRequest(fileName); });
        auto start = chrono::steady_clock::now();
        getTxn.processSync(handle);
        for (const auto& it : getTxn) {
            INFO("state " << static_cast<unsigned>(it.getState()) << " failure " << it.getFailureCode());
            if (it.success())
                CHECK(!content.compare(it.getResult()));
            else
                CHECK(it.getFailureCode() != 0);
        }
        CHECK(chrono::steady_clock::now() - start < 30s);
    }

    SECTION("a truncated tls stream is not a short success") {
        test::TlsProxy innerTls(endpoint);
        test::FaultProxy cut(innerTls.getEndpoint(), test::FaultProxy::Mode::closeMidBody, 4 << 10, -1);
        auto cutProvider = makeProvider(cut.getEndpoint());

        network::Transaction getTxn(cutProvider.get());
        getTxn.verifyKeyRequest(handle, [&]() { return getTxn.getObjectRequest(fileName); });
        auto start = chrono::steady_clock::now();
        getTxn.processSync(handle);
        for (const auto& it : getTxn) {
            if (it.success())
                CHECK(!content.compare(it.getResult()));
            else
                CHECK(it.getFailureCode() != 0);
        }
        CHECK(chrono::steady_clock::now() - start < 30s);
    }

    SECTION("a stalled connection aborts within the deadline") {
        test::FaultProxy faults(endpoint, test::FaultProxy::Mode::headerStall, 1024, -1);
        test::TlsProxy faultyTls(faults.getEndpoint());
        auto faultyProvider = makeProvider(faultyTls.getEndpoint());
        network::Transaction getTxn(faultyProvider.get());
        getTxn.verifyKeyRequest(handle, [&]() { return getTxn.getObjectRequest(fileName); });
        auto start = chrono::steady_clock::now();
        getTxn.processSync(handle);
        for (const auto& it : getTxn)
            CHECK(!it.success());
        CHECK(chrono::steady_clock::now() - start < 20s);
        CHECK(faults.getAccepted() > 1);
    }
    CHECK(group->getInflightMessages() == 0);
}
//---------------------------------------------------------------------------
} // namespace anyblob
