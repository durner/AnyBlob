#include "network/tasked_send_receiver.hpp"
#include "catch2/single_include/catch2/catch.hpp"
#include "perfevent/PerfEvent.hpp"
#include <future>
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
TEST_CASE("send_receiver") {
    PerfEventBlock e;
    auto concurrency = 10;
    uint64_t length = 4096u;

    TaskedSendReceiverGroup group(concurrency << 1, concurrency << 1);

    std::vector<std::unique_ptr<OriginalMessage>> msgs;
    for (auto i = 0; i < concurrency; i++) {
        auto message = std::make_unique<utils::DataVector<uint8_t>>(length);
        std::string str = "GET / HTTP/1.1\r\nHost: db.cs.tum.edu\r\nConnection: keep-alive\r\n\r\n";
        memcpy(message->data(), str.data(), str.length());
        message->resize(str.length());
        msgs.emplace_back(new OriginalMessage {move(message), "db.cs.tum.edu", 80});
        group.send(msgs.back().get());
    }

    group.sendReceive(true);

    std::unique_ptr<utils::DataVector<uint8_t>> currentMessage;
    for (auto i = 0; i < concurrency; i++) {
        auto message = group.receive(msgs[i].get());
        if (i > 0) {
            // skip header
            auto skip = 1024;
            REQUIRE(!std::strncmp(reinterpret_cast<char*>(currentMessage->data()) + skip, reinterpret_cast<char*>(message->data()) + skip, currentMessage->size() - skip));
        }
        currentMessage = move(message);
    }
}
//---------------------------------------------------------------------------
} // namespace test
} // namespace network
} // namespace anyblob
