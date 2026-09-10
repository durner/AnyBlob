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

        // A list request signs its query as part of the canonicalized resource
        dv = azure.listRequest("dir/", "", 0);
        resultString = "GET /test?comp=list&prefix=dir%2F&restype=container HTTP/1.1\r\nAuthorization: SharedKey test:g3JBIJIqZZmimX2WzCY6PXhAkA6LaVRJGHqqrSrmT1w=\r\nHost: test.blob.core.windows.net\r\nx-ms-date: ";
        resultString += azure.fakeXMSTimestamp;
        resultString += "\r\nx-ms-version: 2015-02-21\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);

        // A truncated result is continued with the marker it came back with
        dv = azure.listRequest("dir/", "2!68!MDAwMDI4IWRpci9iLnBhcnF1ZXQh", 1);
        resultString = "GET /test?comp=list&marker=2%2168%21MDAwMDI4IWRpci9iLnBhcnF1ZXQh&maxresults=1&prefix=dir%2F&restype=container HTTP/1.1\r\nAuthorization: SharedKey test:ayl0W6P7oTJqJO5JLD/KX0vj1d0df+G7z2c+hlnYzVo=\r\nHost: test.blob.core.windows.net\r\nx-ms-date: ";
        resultString += azure.fakeXMSTimestamp;
        resultString += "\r\nx-ms-version: 2015-02-21\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);

        // The list result names the blobs of the container
        string listBody = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        listBody += "<EnumerationResults ContainerName=\"https://test.blob.core.windows.net/test\"><Prefix>dir/</Prefix><MaxResults>1000</MaxResults><Blobs>";
        listBody += "<Blob><Name>dir/a.parquet</Name><Properties><Content-Length>12</Content-Length></Properties></Blob>";
        listBody += "<Blob><Name>dir/b.parquet</Name><Properties><Content-Length>24</Content-Length></Properties></Blob>";
        listBody += "</Blobs><NextMarker /></EnumerationResults>";
        string continuationToken = "unset";
        auto keys = azure.getListObjectKeys(listBody, continuationToken);
        REQUIRE(keys.size() == 2);
        REQUIRE(keys[0] == "dir/a.parquet");
        REQUIRE(keys[1] == "dir/b.parquet");
        // A complete result has no marker to continue with
        REQUIRE(continuationToken.empty());

        string truncatedBody = "<EnumerationResults><Blobs><Blob><Name>dir/a.parquet</Name></Blob></Blobs>";
        truncatedBody += "<NextMarker>2!68!MDAwMDI4IWRpci9iLnBhcnF1ZXQh</NextMarker></EnumerationResults>";
        keys = azure.getListObjectKeys(truncatedBody, continuationToken);
        REQUIRE(keys.size() == 1);
        REQUIRE(keys[0] == "dir/a.parquet");
        REQUIRE(continuationToken == "2!68!MDAwMDI4IWRpci9iLnBhcnF1ZXQh");

        // An empty container
        keys = azure.getListObjectKeys("<EnumerationResults><Blobs /><NextMarker /></EnumerationResults>", continuationToken);
        REQUIRE(keys.empty());
        REQUIRE(continuationToken.empty());

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
//---------------------------------------------------------------------------
} // namespace anyblob::cloud::test
