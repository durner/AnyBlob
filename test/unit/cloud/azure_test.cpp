#include "cloud/azure.hpp"
#include "catch2/single_include/catch2/catch.hpp"
#include "cloud/azure_instances.hpp"
#include "cloud/azure_signer.hpp"
#include "utils/data_vector.hpp"
#include <cstdio>
#include <cstring>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::cloud::test {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
// Helper to test private methods
class AzureTester {
    public:
    void test() {
        Provider::testEnviornment = true;

        auto provider = Provider::makeProvider("azure://test/", false, "test", "");
        Azure& azure = *static_cast<Azure*>(provider.get());
        REQUIRE(!azure.getIAMAddress().compare("169.254.169.254"));
        REQUIRE(azure.getIAMPort() == 80);

        auto dv = azure.downloadInstanceInfo();
        string resultString = "GET /metadata/instance?api-version=2021-02-01 HTTP/1.1\r\nHost: 169.254.169.254\r\nMetadata: true\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);

        auto p = pair<uint64_t, uint64_t>(numeric_limits<uint64_t>::max(), numeric_limits<uint64_t>::max());
        dv = azure.getRequest("a/b/c.d", p);
        resultString = "GET /test/a/b/c.d HTTP/1.1\r\nAuthorization: SharedKey test:uhjLcL68dDerTH3WiZ3Zuk0tm3WX+hdmMktg8cYJ74w=\r\nHost: test.blob.core.windows.net\r\nx-ms-date: ";
        resultString += azure.fakeXMSTimestamp;
        resultString += "\r\nx-ms-version: 2015-02-21\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);

        utils::DataVector<uint8_t> putData(10);
        dv = azure.putRequest("a/b/c.d", string_view(reinterpret_cast<const char*>(putData.data()), putData.size()));
        resultString = "PUT /test/a/b/c.d HTTP/1.1\r\nAuthorization: SharedKey test:AiWIKIaUYFV5UOGADs2R+/C8jQu0pW0+lrWV1IfW7Lc=\r\nContent-Length: 10\r\nHost: test.blob.core.windows.net\r\nx-ms-blob-type: BlockBlob\r\nx-ms-date: ";
        resultString += azure.fakeXMSTimestamp;
        resultString += "\r\nx-ms-version: 2015-02-21\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);

        dv = azure.deleteRequest("a/b/c.d");
        resultString = "DELETE /test/a/b/c.d HTTP/1.1\r\nAuthorization: SharedKey test:nuGDW7QRI5/DB5Xt9vET/YEmipJ4UGjn64h4A+BFaL0=\r\nHost: test.blob.core.windows.net\r\nx-ms-date: ";
        resultString += azure.fakeXMSTimestamp;
        resultString += "\r\nx-ms-version: 2015-02-21\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);

        Provider::testEnviornment = false;
    }
};
//---------------------------------------------------------------------------
TEST_CASE("azure") {
    AzureTester tester;
    tester.test();
}
TEST_CASE("azure_bearer") {
    Provider::RemoteInfo info;
    info.provider = Provider::CloudService::Azure;
    info.bucket = "workspace";
    info.endpoint = "onelake.blob.fabric.microsoft.com";
    info.port = 443;
    auto azure = make_unique<Azure>(info, Azure::BearerToken{"test-token"});
    Provider& provider = *azure;
    auto request = provider.getRequest("lakehouse.Lakehouse/Files/data.parquet", {0, 4095});
    string_view text(reinterpret_cast<const char*>(request->data()), request->size());
    REQUIRE(text.starts_with("GET /workspace/lakehouse.Lakehouse/Files/data.parquet HTTP/1.1\r\n"));
    REQUIRE(text.find("Authorization: Bearer test-token\r\n") != text.npos);
    REQUIRE(text.find("Host: onelake.blob.fabric.microsoft.com\r\n") != text.npos);
    REQUIRE(text.find("Range: bytes=0-4095\r\n") != text.npos);
    REQUIRE(text.find("x-ms-version: 2021-06-08\r\n") != text.npos);
    REQUIRE(text.find("SharedKey") == text.npos);
    REQUIRE(provider.getPort() == 443);
    REQUIRE(provider.verifyTlsPeer());
    REQUIRE_THROWS(provider.putRequest("file", "data"));
    REQUIRE_THROWS(provider.deleteRequest("file"));
    REQUIRE_THROWS_AS(Azure(info, Azure::BearerToken{""}), invalid_argument);
    REQUIRE_THROWS_AS(Azure(info, Azure::BearerToken{"bad\r\ntoken"}), invalid_argument);
    info.port = 80;
    REQUIRE_THROWS_AS(Azure(info, Azure::BearerToken{"test-token"}), invalid_argument);
    info.port = 443;
    info.endpoint = "untrusted.example";
    REQUIRE_THROWS_AS(Azure(info, Azure::BearerToken{"test-token"}), invalid_argument);
}
//---------------------------------------------------------------------------
} // namespace anyblob::cloud::test
