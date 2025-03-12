#include "catch2/single_include/catch2/catch.hpp"
#include "cloud/http.hpp"
#include "cloud/provider.hpp"
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
#ifndef NDEBUG
#define verify(expression) assert(expression)
#else
#define verify(expression) ((void) (expression))
#endif
//---------------------------------------------------------------------------
namespace anyblob {
namespace network {
namespace test {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
TEST_CASE("send_receiver") {
    PerfEventBlock e;
    auto concurrency = 8u;
    auto requests = 64u;

    TaskedSendReceiverGroup group;
    group.setConcurrentRequests(concurrency);

    auto provider = cloud::Provider::makeProvider("http://detectportal.firefox.com/success.txt");
    auto tlsProvider = cloud::Provider::makeProvider("https://detectportal.firefox.com/success.txt");

    vector<unique_ptr<OriginalMessage>> msgs;
    for (auto i = 0u; i < requests; i++) {
        auto range = std::pair<uint64_t, uint64_t>(0, 0);
        string file = "";
        msgs.emplace_back(new OriginalMessage{provider->getRequest(file, range), *provider});
        verify(group.send(msgs.back().get()));

        msgs.emplace_back(new OriginalMessage{tlsProvider->getRequest(file, range), *tlsProvider});
        verify(group.send(msgs.back().get()));
    }

    group.process(true);

    unique_ptr<uint8_t[]> currentMessage;
    size_t skip;
    for (auto i = 0u; i < msgs.size();) {
        auto& original = msgs[i];
        switch (original->result.getState()) {
            case MessageState::Finished: {
                auto& message = original->result;
                if (i > 0) {
                    // skip header
                    REQUIRE(!strncmp(reinterpret_cast<char*>(currentMessage.get()) + skip, reinterpret_cast<char*>(message.getData()) + message.getOffset(), message.getSize()));
                }
                currentMessage = message.moveData();
                skip = message.getOffset();
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
