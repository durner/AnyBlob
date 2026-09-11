#include "network/tasked_send_receiver.hpp"
#include "catch2/single_include/catch2/catch.hpp"
#include <chrono>
#include <future>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network::test {
//---------------------------------------------------------------------------
using namespace std;
using namespace std::chrono_literals;
//---------------------------------------------------------------------------
TEST_CASE("tasked_send_receiver") {
}
//---------------------------------------------------------------------------
TEST_CASE("tasked_send_receiver_stop_before_start") {
    TaskedSendReceiverGroup group;
    auto handle = group.getHandle();

    // The stop arrives before the thread enters the deamon
    handle.stop();
    auto deamon = async(launch::async, [&handle]() { handle.process(false); });

    CHECK(deamon.wait_for(5s) == future_status::ready);

    // Never block the test run on a dropped stop
    while (deamon.wait_for(1ms) != future_status::ready)
        handle.stop();
    deamon.get();
}
//---------------------------------------------------------------------------
TEST_CASE("tasked_send_receiver_restart_after_stop") {
    TaskedSendReceiverGroup group;
    auto handle = group.getHandle();

    // End the deamon, a dropped stop may never block the test run
    auto joinDeamon = [&handle](future<void>& deamon) {
        auto ended = deamon.wait_for(5s) == future_status::ready;
        while (deamon.wait_for(1ms) != future_status::ready)
            handle.stop();
        deamon.get();
        return ended;
    };

    // Stop the first run, it may not even have started
    auto deamon = async(launch::async, [&handle]() { handle.process(false); });
    handle.stop();
    CHECK(joinDeamon(deamon));

    // The stop of the first run may not end the second one
    deamon = async(launch::async, [&handle]() { handle.process(false); });
    CHECK(deamon.wait_for(50ms) != future_status::ready);

    handle.stop();
    CHECK(joinDeamon(deamon));
}
//---------------------------------------------------------------------------
} // namespace anyblob::network::test
