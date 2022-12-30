#include "network/io_uring_socket.hpp"
#include "catch2/single_include/catch2/catch.hpp"
#include "perfevent/PerfEvent.hpp"
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace network {
namespace test {
//---------------------------------------------------------------------------
TEST_CASE("io_uring_socket") {
    PerfEventBlock e;
    IOUringSocket io(10, 0);
    IOUringSocket::TCPSettings tcpSettings;
    REQUIRE(io.connect("db.in.tum.de", 80, tcpSettings) > 0);
}
//---------------------------------------------------------------------------
} // namespace test
} // namespace network
} // namespace anyblob
