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
namespace anyblob {
namespace cloud {
namespace test {
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
    auto aws = Provider::makeProvider("s3://x:y");
    REQUIRE(aws->getType() == Provider::CloudService::AWS);
}
//---------------------------------------------------------------------------
} // namespace test
} // namespace cloud
} // namespace anyblob
