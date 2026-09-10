#include "cloud/aws.hpp"
#include "catch2/single_include/catch2/catch.hpp"
#include "cloud/aws_instances.hpp"
#include "cloud/aws_signer.hpp"
#include "utils/data_vector.hpp"
#include <cstring>
#include <iostream>
#include <string_view>
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
class AWSTester {
    public:
    void test() {
        Provider::testEnviornment = true;

        auto provider = Provider::makeProvider("s3://test:test/", false, "test", "test");
        AWS& aws = *static_cast<AWS*>(provider.get());
        REQUIRE(!aws.getIAMAddress().compare("169.254.169.254"));
        REQUIRE(aws.getIAMPort() == 80);

        auto dv = aws.downloadInstanceInfo();
        string resultString = "GET /latest/meta-data/instance-type HTTP/1.1\r\nHost: 169.254.169.254\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);

        dv = aws.downloadIAMUser();
        resultString = "GET /latest/meta-data/iam/security-credentials HTTP/1.1\r\nHost: 169.254.169.254\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);

        string iamUser;
        dv = aws.downloadSecret("ABCDEF\n", iamUser);
        resultString = "GET /latest/meta-data/iam/security-credentials/ABCDEF HTTP/1.1\r\nHost: 169.254.169.254\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);

        string keyService = "{\"AccessKeyId\" : \"ABC\", \"SecretAccessKey\" : \"ABC\", \"Token\" : \"ABC\", \"Expiration\" : \"";
        keyService += aws.fakeIAMTimestamp;
        keyService += "\"}";
        REQUIRE(aws.updateSecret(keyService, iamUser));

        auto p = pair<uint64_t, uint64_t>(numeric_limits<uint64_t>::max(), numeric_limits<uint64_t>::max());
        dv = aws.getRequest("a/b/c.d", p);
        resultString = "GET /a/b/c.d? HTTP/1.1\r\nAuthorization: AWS4-HMAC-SHA256 Credential=ABC/21000101/test/s3/aws4_request, SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-request-payer;x-amz-security-token, Signature=839175aaf3e48a7f0a05fc053f48d1ef731b0fe93bfa6051f596fcce83b2542b\r\nHost: test.s3.test.amazonaws.com\r\nx-amz-content-sha256: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\r\nx-amz-date: ";
        resultString += aws.fakeAMZTimestamp;
        resultString += "\r\nx-amz-request-payer: requester\r\nx-amz-security-token: ABC\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);
        auto dvResigned = aws.resignRequest(*dv.get());
        REQUIRE(string_view(reinterpret_cast<char*>(dvResigned->data()), dvResigned->size()) == resultString);

        utils::DataVector<uint8_t> putData(10);
        dv = aws.putRequest("a/b/c.d", string_view(reinterpret_cast<const char*>(putData.data()), putData.size()));
        resultString = "PUT /a/b/c.d? HTTP/1.1\r\nAuthorization: AWS4-HMAC-SHA256 Credential=ABC/21000101/test/s3/aws4_request, SignedHeaders=content-length;content-md5;host;x-amz-content-sha256;x-amz-date;x-amz-request-payer;x-amz-security-token, Signature=8b1d89369e758299ed4fa88bdb34416b727f9d002bd4fb1a17c6e657d70f3e66\r\nContent-Length: 10\r\nContent-MD5: pjyQzDaErYsKIXamqP6QBQ==\r\nHost: test.s3.test.amazonaws.com\r\nx-amz-content-sha256: 01d448afd928065458cf670b60f5a594d735af0172c8d67f22a81680132681ca\r\nx-amz-date: ";
        resultString += aws.fakeAMZTimestamp;
        resultString += "\r\nx-amz-request-payer: requester\r\nx-amz-security-token: ABC\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);
        dvResigned = aws.resignRequest(*dv.get(), putData.cdata(), putData.size());
        REQUIRE(string_view(reinterpret_cast<char*>(dvResigned->data()), dvResigned->size()) == resultString);

        dv = aws.deleteRequest("a/b/c.d");
        resultString = "DELETE /a/b/c.d? HTTP/1.1\r\nAuthorization: AWS4-HMAC-SHA256 Credential=ABC/21000101/test/s3/aws4_request, SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-request-payer;x-amz-security-token, Signature=2240aba5140727498bd7bcea6f58e68a4c91ef2532b3273834a8d54983ae9319\r\nHost: test.s3.test.amazonaws.com\r\nx-amz-content-sha256: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\r\nx-amz-date: ";
        resultString += aws.fakeAMZTimestamp;
        resultString += "\r\nx-amz-request-payer: requester\r\nx-amz-security-token: ABC\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);
        dvResigned = aws.resignRequest(*dv.get());
        REQUIRE(string_view(reinterpret_cast<char*>(dvResigned->data()), dvResigned->size()) == resultString);

        auto vec = AWSInstance::getInstanceDetails();
        REQUIRE(vec.size() > 0);

        // Signing preserves the inclusive HTTP range for [0, 1 MiB)
        REQUIRE(aws.supportsResigning());
        auto range = pair<uint64_t, uint64_t>(0, 1u << 20);
        dv = aws.getRequest("a/b/c.d", range);
        auto rangedRequest = string(reinterpret_cast<char*>(dv->data()), dv->size());
        REQUIRE(rangedRequest.find("Range: bytes=0-1048575") != string::npos);
        dvResigned = aws.resignRequest(*dv.get());
        REQUIRE(string_view(reinterpret_cast<char*>(dvResigned->data()), dvResigned->size()) == rangedRequest);

        // A single byte range
        range = pair<uint64_t, uint64_t>(0, 1);
        dv = aws.getRequest("a/b/c.d", range);
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()).find("Range: bytes=0-0") != string_view::npos);

        // A list request signs its query, so the whole target has to survive a resign
        dv = aws.listRequest("dir/", "", 0);
        resultString = "GET /?list-type=2&prefix=dir%2F HTTP/1.1\r\nAuthorization: AWS4-HMAC-SHA256 Credential=ABC/21000101/test/s3/aws4_request, SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-request-payer;x-amz-security-token, Signature=c207c3effc015311506838735d1f16ba8ccc795564814187024b46b85f69cdc2\r\nHost: test.s3.test.amazonaws.com\r\nx-amz-content-sha256: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\r\nx-amz-date: ";
        resultString += aws.fakeAMZTimestamp;
        resultString += "\r\nx-amz-request-payer: requester\r\nx-amz-security-token: ABC\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);
        dvResigned = aws.resignRequest(*dv.get());
        REQUIRE(string_view(reinterpret_cast<char*>(dvResigned->data()), dvResigned->size()) == resultString);

        // A truncated result is continued with the token it came back with
        dv = aws.listRequest("dir/", "1ueGcxLPRx1Tr/XYExHnhbYLgveDs2J/wm36Hy4vbOwM=", 1);
        resultString = "GET /?continuation-token=1ueGcxLPRx1Tr%2FXYExHnhbYLgveDs2J%2Fwm36Hy4vbOwM%3D&list-type=2&max-keys=1&prefix=dir%2F HTTP/1.1\r\nAuthorization: AWS4-HMAC-SHA256 Credential=ABC/21000101/test/s3/aws4_request, SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-request-payer;x-amz-security-token, Signature=293b884103946cd960172e764909d982749a16495cc1b13d9532594e18f5324b\r\nHost: test.s3.test.amazonaws.com\r\nx-amz-content-sha256: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\r\nx-amz-date: ";
        resultString += aws.fakeAMZTimestamp;
        resultString += "\r\nx-amz-request-payer: requester\r\nx-amz-security-token: ABC\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);
        dvResigned = aws.resignRequest(*dv.get());
        REQUIRE(string_view(reinterpret_cast<char*>(dvResigned->data()), dvResigned->size()) == resultString);

        // The list result names the keys of the bucket
        string listBody = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        listBody += "<ListBucketResult><Name>test</Name><Prefix>dir/</Prefix><KeyCount>2</KeyCount><MaxKeys>1000</MaxKeys><IsTruncated>false</IsTruncated>";
        listBody += "<Contents><Key>dir/a.parquet</Key><Size>12</Size></Contents>";
        listBody += "<Contents><Key>dir/b.parquet</Key><Size>24</Size></Contents>";
        listBody += "</ListBucketResult>";
        string continuationToken = "unset";
        auto keys = aws.getListObjectKeys(listBody, continuationToken);
        REQUIRE(keys.size() == 2);
        REQUIRE(keys[0] == "dir/a.parquet");
        REQUIRE(keys[1] == "dir/b.parquet");
        // A complete result has no token to continue with
        REQUIRE(continuationToken.empty());

        string truncatedBody = "<ListBucketResult><IsTruncated>true</IsTruncated><Contents><Key>dir/a.parquet</Key></Contents>";
        truncatedBody += "<NextContinuationToken>1ueGcxLPRx1Tr/XYExHnhbYLgveDs2J/wm36Hy4vbOwM=</NextContinuationToken></ListBucketResult>";
        keys = aws.getListObjectKeys(truncatedBody, continuationToken);
        REQUIRE(keys.size() == 1);
        REQUIRE(keys[0] == "dir/a.parquet");
        REQUIRE(continuationToken == "1ueGcxLPRx1Tr/XYExHnhbYLgveDs2J/wm36Hy4vbOwM=");

        // An empty bucket
        keys = aws.getListObjectKeys("<ListBucketResult><KeyCount>0</KeyCount></ListBucketResult>", continuationToken);
        REQUIRE(keys.empty());
        REQUIRE(continuationToken.empty());

        // A public bucket is asked without a signature and without asking it to charge anyone
        auto anonymousProvider = Provider::makeAnonymousProvider("s3://test:test/a/b/c.d");
        AWS& anonymous = *static_cast<AWS*>(anonymousProvider.get());
        dv = anonymous.getRequest("a/b/c.d", p);
        resultString = "GET /a/b/c.d? HTTP/1.1\r\nHost: test.s3.test.amazonaws.com\r\n\r\n";
        REQUIRE(string_view(reinterpret_cast<char*>(dv->data()), dv->size()) == resultString);
        dvResigned = anonymous.resignRequest(*dv.get());
        REQUIRE(string_view(reinterpret_cast<char*>(dvResigned->data()), dvResigned->size()) == resultString);

        // The range of a public bucket survives the resign as well
        dv = anonymous.getRequest("a/b/c.d", range);
        rangedRequest = string(reinterpret_cast<char*>(dv->data()), dv->size());
        REQUIRE(rangedRequest.find("Range: bytes=0-0") != string::npos);
        REQUIRE(rangedRequest.find("Authorization") == string::npos);
        dvResigned = anonymous.resignRequest(*dv.get());
        REQUIRE(string_view(reinterpret_cast<char*>(dvResigned->data()), dvResigned->size()) == rangedRequest);

        Provider::testEnviornment = false;
    }
};
//---------------------------------------------------------------------------
TEST_CASE("aws") {
    AWSTester tester;
    tester.test();
}
//---------------------------------------------------------------------------
} // namespace anyblob::cloud::test
