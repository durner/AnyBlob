#include "cloud/provider.hpp"
#include "catch2/single_include/catch2/catch.hpp"
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
TEST_CASE("provider") {
    REQUIRE(Provider::isRemoteFile("s3://a/b/c"));
    REQUIRE(!Provider::isRemoteFile("a/b/c"));
    REQUIRE(Provider::getRemoteParentDirectory("s3://bucket/b/c") == "b/");
    auto info = Provider::getRemoteInfo("s3://x:y/b");
    REQUIRE(info.bucket == "x");
    REQUIRE(info.region == "y");
    info = Provider::getRemoteInfo("s3://x/b");
    REQUIRE(info.bucket == "x");
    REQUIRE(info.region == "");
}
//---------------------------------------------------------------------------
TEST_CASE("provider_list_objects") {
    std::string body = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    body += "<ListBucketResult><Name>bucket</Name><Prefix>dir/</Prefix><KeyCount>2</KeyCount><MaxKeys>1000</MaxKeys><IsTruncated>false</IsTruncated>";
    body += "<Contents><Key>dir/a.parquet</Key><Size>12</Size></Contents>";
    body += "<Contents><Key>dir/b.parquet</Key><Size>24</Size></Contents>";
    body += "</ListBucketResult>";

    std::string continuationToken = "unset";
    auto keys = Provider::getListObjectKeys(body, continuationToken);
    REQUIRE(keys.size() == 2);
    REQUIRE(keys[0] == "dir/a.parquet");
    REQUIRE(keys[1] == "dir/b.parquet");
    // A complete result has no token to continue with
    REQUIRE(continuationToken.empty());

    std::string truncated = "<ListBucketResult><IsTruncated>true</IsTruncated><Contents><Key>dir/a.parquet</Key></Contents>";
    truncated += "<NextContinuationToken>1ueGcxLPRx1Tr/XYExHnhbYLgveDs2J/wm36Hy4vbOwM=</NextContinuationToken></ListBucketResult>";
    keys = Provider::getListObjectKeys(truncated, continuationToken);
    REQUIRE(keys.size() == 1);
    REQUIRE(continuationToken == "1ueGcxLPRx1Tr/XYExHnhbYLgveDs2J/wm36Hy4vbOwM=");

    // An empty bucket
    keys = Provider::getListObjectKeys("<ListBucketResult><KeyCount>0</KeyCount></ListBucketResult>", continuationToken);
    REQUIRE(keys.empty());
    REQUIRE(continuationToken.empty());
}
//---------------------------------------------------------------------------
TEST_CASE("provider_anonymous") {
    // A public bucket is reachable without credentials
    REQUIRE(Provider::makeAnonymousProvider("s3://bucket:region/dir/file.parquet"));
    REQUIRE(Provider::makeAnonymousProvider("minio://127.0.0.1:9000/bucket:region/dir/file.parquet"));
    REQUIRE(Provider::makeAnonymousProvider("https://host/dir/file.parquet"));

    // These providers sign every request their own way
    REQUIRE_THROWS(Provider::makeAnonymousProvider("azure://container/file.parquet"));
    REQUIRE_THROWS(Provider::makeAnonymousProvider("gs://bucket/file.parquet"));
    REQUIRE_THROWS(Provider::makeAnonymousProvider("oci://bucket:region/file.parquet"));
    REQUIRE_THROWS(Provider::makeAnonymousProvider("ibm://bucket:region/file.parquet"));

    // An s3 express bucket serves nothing without a session
    REQUIRE_THROWS(Provider::makeAnonymousProvider("s3://bucket--x-s3:region/file.parquet"));

    // A virtual hosted bucket has no address without its region
    REQUIRE_THROWS(Provider::makeAnonymousProvider("s3://bucket/file.parquet"));
}
//---------------------------------------------------------------------------
TEST_CASE("provider_object_key") {
    // The bucket addresses the host, so only the remainder is the key
    REQUIRE(Provider::getObjectKey("s3://bucket/dir/file.parquet") == "dir/file.parquet");
    REQUIRE(Provider::getObjectKey("s3://bucket:region/dir/sub/file.parquet") == "dir/sub/file.parquet");
    REQUIRE(Provider::getObjectKey("gs://bucket/dir/file.parquet") == "dir/file.parquet");
    REQUIRE(Provider::getObjectKey("azure://container/file.parquet") == "file.parquet");

    // Path-style endpoints keep the bucket out of the key as well
    REQUIRE(Provider::getObjectKey("minio://127.0.0.1:9000/bucket:region/dir/file.parquet") == "dir/file.parquet");

    // A plain http endpoint knows no bucket, so the whole path is the key
    REQUIRE(Provider::getObjectKey("http://127.0.0.1:9000/bucket/dir/file.parquet") == "bucket/dir/file.parquet");
    REQUIRE(Provider::getObjectKey("https://host/dir/file.parquet") == "dir/file.parquet");

    // An address without a key
    REQUIRE(Provider::getObjectKey("s3://bucket:region/") == "");
    REQUIRE(Provider::getObjectKey("s3://bucket:region") == "");
    REQUIRE(Provider::getObjectKey("minio://127.0.0.1:9000/bucket:region/") == "");
}
//---------------------------------------------------------------------------
TEST_CASE("provider_verify_peer") {
    auto aws = Provider::makeProvider("s3://bucket:region/file", true, "key", "secret");
    REQUIRE(aws->verifyPeer());
    auto https = Provider::makeProvider("https://host/file");
    REQUIRE(https->verifyPeer());
    auto minio = Provider::makeProvider("minio://127.0.0.1:9000/bucket:region/file", false, "key", "secret");
    REQUIRE(!minio->verifyPeer());
    minio->setVerifyPeer(true);
    REQUIRE(minio->verifyPeer());
    https->setVerifyPeer(false);
    REQUIRE(!https->verifyPeer());
}
//---------------------------------------------------------------------------
} // namespace anyblob::cloud::test
