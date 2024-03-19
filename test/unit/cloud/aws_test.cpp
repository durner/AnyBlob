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
namespace anyblob {
namespace cloud {
namespace test {
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

        Provider::testEnviornment = false;
    }
};
//---------------------------------------------------------------------------
TEST_CASE("aws") {
    AWSTester tester;
    tester.test();
}
//---------------------------------------------------------------------------
} // namespace test
} // namespace cloud
} // namespace anyblob
