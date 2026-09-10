#include "catch2/single_include/catch2/catch.hpp"
#include "cloud/aws_cache.hpp"
#include "network/connection_manager.hpp"
#ifdef ANYBLOB_HAS_IO_URING
#include "network/io_uring_socket.hpp"
#endif
#include "perfevent/PerfEvent.hpp"
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network::test {
//---------------------------------------------------------------------------
TEST_CASE("io_uring_socket") {
    PerfEventBlock e;
    ConnectionManager io(10);
    ConnectionManager::TCPSettings tcpSettings;
    REQUIRE(io.connect("db.in.tum.de", 80, false, false, tcpSettings) > 0);
}
//---------------------------------------------------------------------------
#ifdef ANYBLOB_HAS_IO_URING
TEST_CASE("kernel_timespec_normalization") {
    auto ts = IOUringSocket::toKernelTimespec(std::chrono::milliseconds(1500));
    REQUIRE(ts.tv_sec == 1);
    REQUIRE(ts.tv_nsec == 500'000'000);
    ts = IOUringSocket::toKernelTimespec(std::chrono::milliseconds(500));
    REQUIRE(ts.tv_sec == 0);
    REQUIRE(ts.tv_nsec == 500'000'000);
}
#endif
//---------------------------------------------------------------------------
} // namespace anyblob::network::test
