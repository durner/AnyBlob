#include "catch2/single_include/catch2/catch.hpp"
#include "cloud/provider.hpp"
#include "network/message_result.hpp"
#include "network/original_message.hpp"
#include "network/tasked_send_receiver.hpp"
#include "utils/data_vector.hpp"
#include <cstdlib>
#include <string>
#include <utility>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2026
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network::test {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
namespace {
//---------------------------------------------------------------------------
/// Points openssl at an empty trust store
struct TrustStoreGuard {
    /// The previous certificate file
    const char* certFile;
    /// The previous certificate directory
    const char* certDir;

    /// The constructor
    TrustStoreGuard() : certFile(getenv("SSL_CERT_FILE")), certDir(getenv("SSL_CERT_DIR")) {
        setenv("SSL_CERT_FILE", "/dev/null", 1);
        setenv("SSL_CERT_DIR", "/nonexistent", 1);
    }

    /// The destructor
    ~TrustStoreGuard() {
        certFile ? setenv("SSL_CERT_FILE", certFile, 1) : unsetenv("SSL_CERT_FILE");
        certDir ? setenv("SSL_CERT_DIR", certDir, 1) : unsetenv("SSL_CERT_DIR");
    }
};
//---------------------------------------------------------------------------
} // namespace
//---------------------------------------------------------------------------
TEST_CASE("tls_verification") {
    TrustStoreGuard guard;

    TaskedSendReceiverGroup group;
    group.setConcurrentRequests(1);

    auto provider = cloud::Provider::makeProvider("https://detectportal.firefox.com/success.txt");
    auto range = pair<uint64_t, uint64_t>(0, 0);
    string file = "";

    OriginalMessage verified{provider->getRequest(file, range), *provider};
    REQUIRE(group.send(&verified));
    group.process(true);
    REQUIRE(verified.result.getState() == MessageState::Finished);

    provider->setVerifyPeer(false);
    OriginalMessage unverified{provider->getRequest(file, range), *provider};
    REQUIRE(group.send(&unverified));
    group.process(true);
    REQUIRE(unverified.result.getState() == MessageState::Finished);
}
//---------------------------------------------------------------------------
} // namespace anyblob::network::test
