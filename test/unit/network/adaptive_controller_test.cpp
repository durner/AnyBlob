#include "network/adaptive_controller.hpp"
#include "catch2/single_include/catch2/catch.hpp"
#include "network/tasked_send_receiver.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>
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
using namespace std;
using namespace std::chrono_literals;
//---------------------------------------------------------------------------
/// The machine the controller cases model, the host must not change a result
static constexpr auto hardwareThreads = 24u;
//---------------------------------------------------------------------------
/// A controller on the modeled machine
static AdaptiveController makeController(unsigned seed = 0, unsigned maxRequests = 32) {
    return AdaptiveController(maxRequests, seed, hardwareThreads);
}
//---------------------------------------------------------------------------
/// Builds a sample of one second
static AdaptiveController::Sample sampleThroughput(uint64_t bytesPerSec, uint64_t queued = 1000, uint64_t inflight = 1000) {
    return {bytesPerSec, std::chrono::nanoseconds(1s), queued, inflight};
}
//---------------------------------------------------------------------------
/// Model throughput as per-request rate
static uint64_t modelThroughput(const AdaptiveController::Recommendation& rec, uint64_t perRequestBps, uint64_t linkBps) {
    return std::min(linkBps, static_cast<uint64_t>(rec.threads) * rec.requestsPerThread * perRequestBps);
}
//---------------------------------------------------------------------------
/// Limit throughput by link, thread, and request capacity
static uint64_t modelCeiling(const AdaptiveController::Recommendation& rec, uint64_t linkBps, uint64_t threadBps, uint64_t requestBps, uint64_t knee = 0) {
    auto concurrency = static_cast<uint64_t>(rec.threads) * rec.requestsPerThread;
    auto perThread = static_cast<double>(threadBps);
    if (knee && concurrency > knee)
        perThread = perThread * static_cast<double>(knee) / static_cast<double>(knee + (concurrency - knee) / 4);
    auto threadCeiling = static_cast<uint64_t>(perThread * rec.threads);
    return std::min(linkBps, std::min(threadCeiling, concurrency * requestBps));
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_seeding") {
    auto controller = makeController();
    REQUIRE(controller.current().threads == hardwareThreads / 8);
    REQUIRE(controller.current().requestsPerThread == 16);
    REQUIRE(controller.maxThreads() == hardwareThreads);

    // Without a bound the hardware provides one
    AdaptiveController detected(32);
    REQUIRE(detected.maxThreads() == std::max(1u, std::thread::hardware_concurrency()));
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_climb_to_plateau") {
    auto controller = makeController();
    uint64_t perRequest = 25'000'000;
    uint64_t link = 2'000'000'000;

    auto rec = controller.current();
    double settledBps = 0;
    auto settledEpochs = 0u;
    for (auto i = 0u; i < 80; i++) {
        auto bps = modelThroughput(rec, perRequest, link);
        if (i >= 50) {
            settledBps += static_cast<double>(bps);
            settledEpochs++;
        }
        rec = controller.measure(sampleThroughput(bps));
        REQUIRE(rec.requestsPerThread <= 32);
    }
    REQUIRE(settledBps / settledEpochs >= 0.93 * static_cast<double>(link));
    REQUIRE(rec.threads <= 5);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_cpu_bound_growth") {
    auto controller = makeController();
    auto seed = controller.current();

    // CPU-bound scaling
    auto rec = seed;
    for (auto i = 0u; i < 80; i++) {
        auto bps = static_cast<uint64_t>(rec.threads) * 1'000'000'000;
        rec = controller.measure(sampleThroughput(bps));
    }
    // Requests never help a thread bound workload
    REQUIRE(rec.requestsPerThread <= seed.requestsPerThread);
    REQUIRE(rec.threads >= hardwareThreads - 2);
    REQUIRE(rec.threads >= 4 * seed.threads);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_warmup_no_overshoot") {
    auto controller = makeController();
    auto seed = controller.current();

    // Connection ramp
    double ramp[] = {0.2, 0.45, 0.7, 0.9, 1.0};
    auto rec = seed;
    for (auto factor : ramp) {
        rec = controller.measure(sampleThroughput(static_cast<uint64_t>(factor * 1e9)));
        REQUIRE(rec == seed);
    }
    rec = controller.measure(sampleThroughput(1'000'000'000));
    REQUIRE(rec.requestsPerThread > seed.requestsPerThread);
    REQUIRE(rec.threads == seed.threads);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_growth_gate_noise") {
    auto controller = makeController();
    auto seed = controller.current();

    for (auto i = 0u; i < 25; i++) {
        auto rec = controller.measure(sampleThroughput(i % 2 ? 1'000'000'000 : 800'000'000));
        REQUIRE(rec == seed);
    }
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_no_false_idle") {
    auto controller = makeController();
    auto before = controller.current();

    auto rec = controller.measure({0, std::chrono::nanoseconds(1s), 0, 5});
    REQUIRE(rec.threads == before.threads);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_idle_and_reattach") {
    auto controller = makeController();
    auto seed = controller.current();
    uint64_t perRequest = 25'000'000;
    uint64_t link = 1'000'000'000'000;

    // One settle epoch, one growth step, one kept judgement
    auto rec = controller.current();
    for (auto i = 0u; i < 3; i++)
        rec = controller.measure(sampleThroughput(modelThroughput(rec, perRequest, link)));
    auto grown = rec;
    REQUIRE(grown != seed);

    // An idle epoch releases the threads
    rec = controller.measure({0, std::chrono::nanoseconds(1s), 0, 0});
    REQUIRE(rec.threads == 1);
    REQUIRE(rec.requestsPerThread == grown.requestsPerThread);

    // Returning demand reattaches
    rec = controller.measure(sampleThroughput(modelThroughput(grown, perRequest, link)));
    REQUIRE(rec == grown);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_degradation_recovery") {
    auto controller = makeController();
    uint64_t link = 2'000'000'000;

    // Converge to the plateau
    auto rec = controller.current();
    for (auto i = 0u; i < 12; i++)
        rec = controller.measure(sampleThroughput(modelThroughput(rec, 25'000'000, link)));

    // The per-request throughput collapses
    double settledBps = 0;
    auto settledEpochs = 0u;
    for (auto i = 0u; i < 60; i++) {
        auto bps = modelThroughput(rec, 5'000'000, link);
        if (i >= 40) {
            settledBps += static_cast<double>(bps);
            settledEpochs++;
        }
        rec = controller.measure(sampleThroughput(bps));
    }
    REQUIRE(settledBps / settledEpochs >= 0.9 * static_cast<double>(link));
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_partial_recovery") {
    auto controller = makeController();
    uint64_t link = 2'000'000'000;

    // Converge to the plateau
    auto rec = controller.current();
    for (auto i = 0u; i < 40; i++)
        rec = controller.measure(sampleThroughput(modelThroughput(rec, 25'000'000, link)));

    // The per-request throughput collapses and the link caps below the old best
    uint64_t capped = 1'350'000'000;
    double settledBps = 0;
    auto settledEpochs = 0u;
    for (auto i = 0u; i < 100; i++) {
        auto bps = modelThroughput(rec, 5'000'000, capped);
        if (i >= 80) {
            settledBps += static_cast<double>(bps);
            settledEpochs++;
        }
        rec = controller.measure(sampleThroughput(bps));
    }
    REQUIRE(settledBps / settledEpochs >= 0.9 * static_cast<double>(capped));
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_requests_before_threads") {
    auto controller = makeController();
    auto seed = controller.current();
    // Seeded threads can saturate the link at maximum requests
    uint64_t perRequest = 25'000'000;
    uint64_t link = static_cast<uint64_t>(seed.threads) * 32 * perRequest;
    auto rec = seed;
    for (auto i = 0u; i < 60; i++) {
        rec = controller.measure(sampleThroughput(modelCeiling(rec, link, link, perRequest)));
        REQUIRE(rec.threads <= seed.threads + 1);
    }
    REQUIRE(rec.requestsPerThread == 32);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_requests_shrink_under_contention") {
    auto controller = makeController();
    // TLS model: thread-bound with contention above the knee
    uint64_t link = 36'250'000'000;
    auto rec = controller.current();
    for (auto i = 0u; i < 200; i++)
        rec = controller.measure(sampleThroughput(modelCeiling(rec, link, 950'000'000, 76'250'000, 480)));
    REQUIRE(rec.requestsPerThread <= 16);
    REQUIRE(rec.threads >= 12);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_no_ratchet_under_noise") {
    auto controller = makeController();
    uint64_t link = 2'000'000'000;
    auto rec = controller.current();
    // Converge first
    for (auto i = 0u; i < 40; i++)
        rec = controller.measure(sampleThroughput(modelThroughput(rec, 25'000'000, link)));

    // Use the median to tolerate probes while detecting sustained throughput loss
    std::vector<uint64_t> settled;
    for (auto i = 0u; i < 300; i++) {
        auto clean = modelThroughput(rec, 25'000'000, link);
        if (i > 100)
            settled.push_back(clean);
        auto jitter = 1.0 + ((i * 2654435761u) % 800) / 10000.0 - 0.04;
        rec = controller.measure(sampleThroughput(static_cast<uint64_t>(static_cast<double>(clean) * jitter)));
    }
    std::sort(settled.begin(), settled.end());
    REQUIRE(static_cast<double>(settled[settled.size() / 2]) >= 0.95 * static_cast<double>(link));
    REQUIRE(static_cast<double>(modelThroughput(rec, 25'000'000, link)) >= 0.95 * static_cast<double>(link));
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_sheds_below_the_seed_on_a_capped_link") {
    auto controller = makeController();
    auto seed = controller.current();
    // Half the seeded concurrency saturates the link
    uint64_t perRequest = 25'000'000;
    uint64_t link = static_cast<uint64_t>(seed.threads) * seed.requestsPerThread * perRequest / 2;

    auto rec = seed;
    for (auto i = 0u; i < 200; i++) {
        auto clean = modelThroughput(rec, perRequest, link);
        // Inject an early burst above the plateau
        auto reported = i == 1 ? static_cast<uint64_t>(static_cast<double>(clean) * 1.1) : clean;
        rec = controller.measure(sampleThroughput(reported));
    }
    REQUIRE(rec.threads < seed.threads);
    REQUIRE(static_cast<double>(modelThroughput(rec, perRequest, link)) >= 0.95 * static_cast<double>(link));
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_requests_ceiling") {
    auto controller = makeController(0, 8);
    REQUIRE(controller.current().requestsPerThread == 4);
    uint64_t link = 1'000'000'000'000;
    auto rec = controller.current();
    for (auto i = 0u; i < 80; i++) {
        rec = controller.measure(sampleThroughput(modelCeiling(rec, link, link, 25'000'000)));
        REQUIRE(rec.requestsPerThread <= 8);
    }
    REQUIRE(rec.requestsPerThread == 8);
    REQUIRE(rec.threads > controller.maxThreads() / 8);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_seed_does_not_pick_the_fixed_point") {
    // Four threads saturate the link
    uint64_t link = 4'000'000'000;
    uint64_t threadBps = 1'000'000'000;
    uint64_t requestBps = 40'000'000;

    // Count epochs to the target under noise, capped at the budget
    auto epochsToReach = [&](unsigned seed, unsigned target, unsigned budget) {
        auto controller = makeController(seed);
        auto rec = controller.current();
        for (auto i = 0u; i < budget; i++) {
            auto clean = modelCeiling(rec, link, threadBps, requestBps);
            auto jitter = 1.0 + ((i * 2654435761u) % 800) / 10000.0 - 0.04;
            rec = controller.measure(sampleThroughput(static_cast<uint64_t>(static_cast<double>(clean) * jitter)));
            if (rec.threads <= target)
                return i;
        }
        return budget;
    };

    REQUIRE(epochsToReach(hardwareThreads, hardwareThreads / 2, 400) < 100);

    // Both seeds should converge under noise
    auto settle = [&](unsigned seed) {
        auto controller = makeController(seed);
        auto rec = controller.current();
        for (auto i = 0u; i < 400; i++) {
            auto clean = modelCeiling(rec, link, threadBps, requestBps);
            auto jitter = 1.0 + ((i * 2654435761u) % 800) / 10000.0 - 0.04;
            rec = controller.measure(sampleThroughput(static_cast<uint64_t>(static_cast<double>(clean) * jitter)));
        }
        return rec;
    };
    auto low = settle(1);
    auto high = settle(hardwareThreads);
    REQUIRE(high.threads <= low.threads + 4);
    REQUIRE(modelCeiling(high, link, threadBps, requestBps) >= link - link / 10);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_trades_threads_for_requests") {
    // Request concurrency limits throughput
    uint64_t link = 4'000'000'000;
    uint64_t threadBps = 4'000'000'000;
    uint64_t requestBps = link / 480;

    auto controller = makeController(hardwareThreads);
    auto rec = controller.current();
    for (auto i = 0u; i < 300; i++)
        rec = controller.measure(sampleThroughput(modelCeiling(rec, link, threadBps, requestBps)));

    REQUIRE(rec.requestsPerThread == 32);
    REQUIRE(rec.threads <= 20);
    REQUIRE(modelCeiling(rec, link, threadBps, requestBps) >= link - link / 10);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_seed_from_config") {
    auto model = [](uint64_t network) { return Config{Config::defaultCoreThroughput, Config::defaultCoreConcurrency, network}; };

    // Measured HTTP/TLS optima: 16/44 threads at 300 Gbit/s, 4/12 at 50 Gbit/s
    REQUIRE(AdaptiveController::seedThreads(model(300'000), false, 96) == 19);
    REQUIRE(AdaptiveController::seedThreads(model(300'000), true, 96) == 38);
    REQUIRE(AdaptiveController::seedThreads(model(50'000), false, 36) == 4);
    REQUIRE(AdaptiveController::seedThreads(model(50'000), true, 36) == 7);

    // Unknown bandwidth uses hardware concurrency
    REQUIRE(AdaptiveController::seedThreads(model(0), true, 96) == 12);
    REQUIRE(AdaptiveController::seedThreads(model(0), false, 4) == 1);

    REQUIRE(AdaptiveController::seedThreads(model(600'000), true, 8) == 8);
    REQUIRE(AdaptiveController::seedThreads(model(1'000), false, 8) == 1);

    // The seed applies up to the hardware bound
    auto seeded = makeController(19);
    REQUIRE(seeded.current().threads == 19);
    REQUIRE(seeded.current().requestsPerThread == 16);
    auto capped = makeController(2 * hardwareThreads);
    REQUIRE(capped.current().threads == hardwareThreads);
    auto unseeded = makeController();
    REQUIRE(unseeded.current().threads == hardwareThreads / 8);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_group_counters") {
    TaskedSendReceiverGroup group;
    REQUIRE(group.maxConcurrentRequests() == 32);
    group.setConcurrentRequests(10);
    REQUIRE(group.getConcurrentRequests() == 10);
    group.setConcurrentRequests(32);
    REQUIRE(group.getConcurrentRequests() == 32);
    REQUIRE(group.getTransferredBytes() == 0);
    REQUIRE(group.getQueuedMessages() == 0);
    REQUIRE(group.getInflightMessages() == 0);
}
//---------------------------------------------------------------------------
} // namespace anyblob::network::test
