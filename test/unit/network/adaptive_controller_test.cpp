#include "catch2/single_include/catch2/catch.hpp"
#include "network/adaptive_controller.hpp"
#include "network/tasked_send_receiver.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2026
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network::test {
//---------------------------------------------------------------------------
using namespace std::chrono_literals;
using Controller = AdaptiveController;
//---------------------------------------------------------------------------
namespace {
//---------------------------------------------------------------------------
/// Builds a sample worth one second with the given throughput in bytes/s
Controller::Sample sampleBps(uint64_t bytesPerSec, uint64_t queued = 1000, uint64_t inflight = 1000) {
    return {bytesPerSec, std::chrono::nanoseconds(1s), queued, inflight};
}
//---------------------------------------------------------------------------
/// Feed identical samples for a number of epochs
Controller::Recommendation feedEpochs(Controller& controller, uint64_t bytesPerSec, unsigned epochs, uint64_t queued = 1000, uint64_t inflight = 1000) {
    Controller::Recommendation rec = controller.current();
    for (auto i = 0u; i < epochs; i++)
        rec = controller.update(sampleBps(bytesPerSec, queued, inflight));
    return rec;
}
//---------------------------------------------------------------------------
/// Model throughput as per-request rate capped by the link capacity
uint64_t modelBps(const Controller::Recommendation& rec, uint64_t perRequestBps, uint64_t linkBps) {
    return std::min(linkBps, static_cast<uint64_t>(rec.threads) * rec.requestsPerThread * perRequestBps);
}
//---------------------------------------------------------------------------
} // namespace
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_seeding") {
    auto hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
    // Emulates a c5n.18xlarge with 100 Gbit network and the static model of 8000 Mbit per core
    Config config = {Config::defaultCoreThroughput, Config::defaultCoreConcurrency, 100000};
    Controller controller(config, 32);
    auto rec = controller.current();
    REQUIRE(rec.threads == std::min(13u, hardwareThreads));
    REQUIRE(rec.requestsPerThread == 20);
    // The static model only seeds while adaptivity may use all cores
    REQUIRE(controller.maxThreads() == hardwareThreads);

    // Unknown bandwidth seeds small
    Config unknownConfig = {Config::defaultCoreThroughput, Config::defaultCoreConcurrency, 0};
    Controller gradientController(unknownConfig, 32);
    REQUIRE(gradientController.current().threads >= 1);
    REQUIRE(gradientController.current().threads <= hardwareThreads);
    REQUIRE(gradientController.current().requestsPerThread == 20);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_measured_jump") {
    if (std::thread::hardware_concurrency() < 4)
        return;
    // A small 16 Gbit target with 2 seed threads to be independent of the test machine
    Config config = {Config::defaultCoreThroughput, Config::defaultCoreConcurrency, 16000};
    Controller controller(config, 32);
    REQUIRE(controller.current() == Controller::Recommendation{2, 20});

    // 25 MB/s per request needs 80 concurrent requests for the target
    uint64_t perRequest = 25'000'000;
    uint64_t link = 2'000'000'000;
    auto rec = controller.current();
    rec = controller.update(sampleBps(modelBps(rec, perRequest, link)));
    // One measured jump instead of blind steps, packed into 3 threads instead of 4
    REQUIRE(static_cast<uint64_t>(rec.threads) * rec.requestsPerThread >= 80);
    REQUIRE(rec.threads == 3);

    // The jump is kept and the configuration is stable afterwards
    rec = controller.update(sampleBps(modelBps(rec, perRequest, link)));
    auto stable = rec;
    rec = feedEpochs(controller, modelBps(rec, perRequest, link), 5);
    REQUIRE(rec == stable);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_packs_requests") {
    if (std::thread::hardware_concurrency() < 2)
        return;
    Config config = {Config::defaultCoreThroughput, Config::defaultCoreConcurrency, 16000};
    Controller controller(config, 32);

    // The needed 64 concurrent requests fit into the 2 seeded threads
    uint64_t perRequest = 31'250'000;
    uint64_t link = 2'000'000'000;
    auto rec = controller.current();
    for (auto i = 0u; i < 5; i++)
        rec = controller.update(sampleBps(modelBps(rec, perRequest, link)));
    REQUIRE(rec.threads == 2);
    REQUIRE(rec.requestsPerThread == 32);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_no_false_idle") {
    Config config = {Config::defaultCoreThroughput, Config::defaultCoreConcurrency, 16000};
    Controller controller(config, 32);
    auto before = controller.current();

    // In-flight requests without queued messages are not idle
    auto rec = controller.update({0, std::chrono::nanoseconds(1s), 0, 5});
    REQUIRE(rec.threads == before.threads);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_idle_and_reattach") {
    if (std::thread::hardware_concurrency() < 4)
        return;
    Config config = {Config::defaultCoreThroughput, Config::defaultCoreConcurrency, 16000};
    Controller controller(config, 32);
    uint64_t perRequest = 25'000'000;
    uint64_t link = 2'000'000'000;

    // Grow one kept jump so the working configuration differs from the seed
    auto rec = controller.current();
    rec = controller.update(sampleBps(modelBps(rec, perRequest, link)));
    rec = controller.update(sampleBps(modelBps(rec, perRequest, link)));
    auto grown = rec;
    REQUIRE(static_cast<uint64_t>(grown.threads) * grown.requestsPerThread > 40);

    // An idle epoch releases the threads while the requests knob stays untouched
    rec = controller.update({0, std::chrono::nanoseconds(1s), 0, 0});
    REQUIRE(rec.threads == 1);
    REQUIRE(rec.requestsPerThread == grown.requestsPerThread);

    // Returning demand reattaches to the last working configuration
    rec = controller.update(sampleBps(modelBps(grown, perRequest, link)));
    REQUIRE(rec == grown);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_probe_down") {
    if (std::thread::hardware_concurrency() < 2)
        return;
    Config config = {Config::defaultCoreThroughput, Config::defaultCoreConcurrency, 16000};
    Controller controller(config, 32);
    auto seed = controller.current();
    // The seed meets the target exactly
    uint64_t perRequest = 50'000'000;
    uint64_t link = 2'000'000'000;

    // Meets the target and holds until the first probe epoch
    auto rec = feedEpochs(controller, modelBps(seed, perRequest, link), Controller::probeInterval - 1);
    REQUIRE(rec == seed);

    // The probe removes a thread first to minimize cores
    rec = feedEpochs(controller, modelBps(rec, perRequest, link), 1);
    REQUIRE(rec.threads == seed.threads - 1);

    // The probe lost the target and is reverted
    rec = controller.update(sampleBps(modelBps(rec, perRequest, link)));
    REQUIRE(rec == seed);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_degradation_regrow") {
    if (std::thread::hardware_concurrency() < 5)
        return;
    Config config = {Config::defaultCoreThroughput, Config::defaultCoreConcurrency, 16000};
    Controller controller(config, 32);
    uint64_t link = 2'000'000'000;

    // The seed holds the target at the static model throughput
    auto rec = controller.current();
    for (auto i = 0u; i < 3; i++)
        rec = controller.update(sampleBps(modelBps(rec, 50'000'000, link)));
    REQUIRE(rec == controller.current());

    // A noisy neighbour quarters the per-request throughput
    uint64_t degraded = 12'500'000;
    for (auto i = 0u; i < 6; i++)
        rec = controller.update(sampleBps(modelBps(rec, degraded, link)));
    REQUIRE(modelBps(rec, degraded, link) >= static_cast<uint64_t>(0.98 * static_cast<double>(link)));
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_unknown_bandwidth") {
    if (std::thread::hardware_concurrency() < 4)
        return;
    // Unknown bandwidth climbs to the measured plateau
    Config config = {Config::defaultCoreThroughput, Config::defaultCoreConcurrency, 0};
    Controller controller(config, 32);
    uint64_t perRequest = 10'000'000;
    // The link saturates at 100 concurrent requests
    uint64_t link = 1'000'000'000;

    auto rec = controller.current();
    double settledBps = 0;
    auto settledEpochs = 0u;
    for (auto i = 0u; i < 100; i++) {
        auto bps = modelBps(rec, perRequest, link);
        if (i >= 50) {
            settledBps += static_cast<double>(bps);
            settledEpochs++;
        }
        rec = controller.update(sampleBps(bps));
    }
    // The plateau is fully attained and the configuration does not run away
    REQUIRE(settledBps / settledEpochs >= 0.99 * static_cast<double>(link));
    REQUIRE(static_cast<uint64_t>(rec.threads) * rec.requestsPerThread <= 240);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_group_counters") {
    TaskedSendReceiverGroup group;
    REQUIRE(group.maxConcurrentRequests() == 32);
    group.setConcurrentRequests(10);
    REQUIRE(group.getConcurrentRequests() == 10);
    group.setConcurrentRequests(32);
    REQUIRE(group.getConcurrentRequests() == 32);

    // The counters used for sampling start at zero
    REQUIRE(group.getTransferredBytes() == 0);
    REQUIRE(group.getQueuedMessages() == 0);
    REQUIRE(group.getInflightMessages() == 0);
}
//---------------------------------------------------------------------------
} // namespace anyblob::network::test
