#include "catch2/single_include/catch2/catch.hpp"
#include "network/original_message.hpp"
#include "network/tasked_send_receiver.hpp"
#include "perfevent/PerfEvent.hpp"
#include "utils/data_vector.hpp"
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
using namespace std;
//---------------------------------------------------------------------------
TEST_CASE("send_receiver") {
    PerfEventBlock e;
    auto concurrency = 10;
    uint64_t length = 4096u;

    TaskedSendReceiverGroup group(concurrency << 1, concurrency << 1);

    vector<unique_ptr<OriginalMessage>> msgs;
    for (auto i = 0; i < concurrency; i++) {
        auto message = make_unique<utils::DataVector<uint8_t>>(length);
        string str = "GET / HTTP/1.1\r\nHost: db.cs.tum.edu\r\nConnection: keep-alive\r\n\r\n";
        memcpy(message->data(), str.data(), str.length());
        message->resize(str.length());
        msgs.emplace_back(new OriginalMessage{move(message), "db.cs.tum.edu", 80});
        group.send(msgs.back().get());
    }

    group.process(true);

    unique_ptr<uint8_t[]> currentMessage;
    size_t size;
    for (auto i = 0; i < concurrency; i++) {
        auto& original = msgs[i];
        switch (original->result.getState()) {
            case MessageState::Finished: {
                auto& message = original->result;
                if (i > 0) {
                    // skip header
                    auto skip = 1024;
                    REQUIRE(!strncmp(reinterpret_cast<char*>(currentMessage.get()) + skip, reinterpret_cast<char*>(message.getData()) + skip, size - skip));
                }
                currentMessage = message.moveData();
                size = message.getOffset() + message.getSize();
                ++i;
                break;
            }
            default:
                REQUIRE(original->result.getState() != MessageState::Aborted);
        }
    }
}
//---------------------------------------------------------------------------
} // namespace test
} // namespace network
} // namespace anyblob
