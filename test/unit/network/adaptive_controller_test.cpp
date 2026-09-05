#include "network/adaptive_controller.hpp"
#include "catch2/single_include/catch2/catch.hpp"
#include "network/config.hpp"
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
/// The group the controller cases model, a thread may not run more requests
static constexpr auto maxRequests = 32u;
//---------------------------------------------------------------------------
/// The instance config of the modeled machine, the network is what seeds the threads
static Config modelConfig(uint64_t network) {
    return Config{Config::defaultCoreThroughput, Config::defaultCoreConcurrency, network};
}
//---------------------------------------------------------------------------
/// A controller on the modeled machine
static AdaptiveController makeController(unsigned seed = 0) {
    return AdaptiveController(modelConfig(2 * seed * Config::defaultCoreThroughput), false, hardwareThreads);
}
//---------------------------------------------------------------------------
/// Builds a sample of one second for a consumer that applied the recommendation
static AdaptiveController::Sample sampleThroughput(const AdaptiveController::Recommendation& applied, uint64_t bytesPerSec, unsigned ceiling = maxRequests, uint64_t queued = 1000, uint64_t inflight = 1000) {
    return {bytesPerSec, std::chrono::nanoseconds(1s), queued, inflight, applied, ceiling};
}
//---------------------------------------------------------------------------
/// Builds a sample of one second without any demand
static AdaptiveController::Sample sampleIdle(const AdaptiveController::Recommendation& applied, uint64_t inflight = 0) {
    return {0, std::chrono::nanoseconds(1s), 0, inflight, applied, maxRequests};
}
//---------------------------------------------------------------------------
/// The configuration the controller holds, the most frequent of a tail skips the probes
static AdaptiveController::Recommendation held(const std::vector<AdaptiveController::Recommendation>& epochs) {
    static constexpr auto window = 40u;
    auto begin = epochs.size() > window ? epochs.size() - window : 0u;
    auto best = epochs[begin];
    auto bestCount = 0u;
    for (auto i = begin; i < epochs.size(); i++) {
        auto count = 0u;
        for (auto j = begin; j < epochs.size(); j++)
            if (epochs[j] == epochs[i])
                count++;
        if (count > bestCount) {
            bestCount = count;
            best = epochs[i];
        }
    }
    return best;
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
    CHECK(controller.current().threads == hardwareThreads / 8);
    CHECK(controller.current().requestsPerThread == Config::defaultCoreConcurrency);
    CHECK(controller.maxThreads() == hardwareThreads);

    // Without a bound the hardware provides one
    AdaptiveController detected(modelConfig(0), false);
    CHECK(detected.maxThreads() == std::max(1u, std::thread::hardware_concurrency()));
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
        rec = controller.measure(sampleThroughput(rec, bps));
        CHECK(rec.requestsPerThread <= 32);
    }
    CHECK(settledBps / settledEpochs >= 0.93 * static_cast<double>(link));
    CHECK(rec.threads <= 5);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_cpu_bound_growth") {
    auto controller = makeController();
    auto seed = controller.current();

    // CPU-bound scaling
    auto rec = seed;
    std::vector<AdaptiveController::Recommendation> epochs;
    for (auto i = 0u; i < 80; i++) {
        auto bps = static_cast<uint64_t>(rec.threads) * 1'000'000'000;
        rec = controller.measure(sampleThroughput(rec, bps));
        epochs.push_back(rec);
    }
    // Requests never help a thread bound workload
    auto settled = held(epochs);
    CHECK(settled.requestsPerThread <= seed.requestsPerThread);
    CHECK(settled.threads >= hardwareThreads - 2);
    CHECK(settled.threads >= 4 * seed.threads);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_warmup_no_overshoot") {
    auto controller = makeController();
    auto seed = controller.current();

    // Connection ramp
    double ramp[] = {0.2, 0.45, 0.7, 0.9, 1.0};
    auto rec = seed;
    for (auto factor : ramp) {
        rec = controller.measure(sampleThroughput(rec, static_cast<uint64_t>(factor * 1e9)));
        CHECK(rec == seed);
    }
    rec = controller.measure(sampleThroughput(rec, 1'000'000'000));
    CHECK(rec.requestsPerThread > seed.requestsPerThread);
    CHECK(rec.threads == seed.threads);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_growth_gate_noise") {
    auto controller = makeController();
    auto seed = controller.current();

    auto rec = seed;
    for (auto i = 0u; i < 25; i++) {
        rec = controller.measure(sampleThroughput(rec, i % 2 ? 1'000'000'000 : 800'000'000));
        CHECK(rec == seed);
    }
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_no_false_idle") {
    auto controller = makeController();
    auto before = controller.current();

    auto rec = controller.measure(sampleIdle(before, 5));
    CHECK(rec.threads == before.threads);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_idle_and_reattach") {
    auto controller = makeController();
    auto seed = controller.current();
    uint64_t perRequest = 25'000'000;
    uint64_t link = 1'000'000'000'000;

    // One settle epoch, one growth step, one epoch in effect, one kept judgement
    auto rec = controller.current();
    for (auto i = 0u; i < 4; i++)
        rec = controller.measure(sampleThroughput(rec, modelThroughput(rec, perRequest, link)));
    auto grown = rec;
    CHECK(grown != seed);

    // An idle epoch releases the threads
    rec = controller.measure(sampleIdle(grown));
    CHECK(rec.threads == 1);
    CHECK(rec.requestsPerThread == grown.requestsPerThread);

    // Returning demand reattaches
    rec = controller.measure(sampleThroughput(grown, modelThroughput(grown, perRequest, link)));
    CHECK(rec == grown);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_waits_for_a_lagging_consumer") {
    auto controller = makeController();
    auto seed = controller.current();
    uint64_t perRequest = 25'000'000;
    uint64_t link = 1'000'000'000'000;

    // The consumer keeps running the seed whatever the controller asks for
    auto rec = seed;
    for (auto i = 0u; i < 40; i++)
        rec = controller.measure(sampleThroughput(seed, modelThroughput(seed, perRequest, link)));

    // A configuration that never ran earns no verdict, so the ask stays one step from reality
    CHECK(rec.threads == seed.threads);
    CHECK(rec.requestsPerThread > seed.requestsPerThread);
    CHECK(rec.requestsPerThread <= 30);

    // Once the consumer catches up the step is judged, kept, and followed by the next one
    auto asked = rec;
    for (auto i = 0u; i < 4; i++)
        rec = controller.measure(sampleThroughput(asked, modelThroughput(asked, perRequest, link)));
    CHECK(rec.requestsPerThread > asked.requestsPerThread);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_degradation_recovery") {
    auto controller = makeController();
    uint64_t link = 2'000'000'000;

    // Converge to the plateau
    auto rec = controller.current();
    for (auto i = 0u; i < 12; i++)
        rec = controller.measure(sampleThroughput(rec, modelThroughput(rec, 25'000'000, link)));

    // The per-request throughput collapses
    double settledBps = 0;
    auto settledEpochs = 0u;
    for (auto i = 0u; i < 60; i++) {
        auto bps = modelThroughput(rec, 5'000'000, link);
        if (i >= 40) {
            settledBps += static_cast<double>(bps);
            settledEpochs++;
        }
        rec = controller.measure(sampleThroughput(rec, bps));
    }
    CHECK(settledBps / settledEpochs >= 0.9 * static_cast<double>(link));
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_partial_recovery") {
    auto controller = makeController();
    uint64_t link = 2'000'000'000;

    // Converge to the plateau
    auto rec = controller.current();
    for (auto i = 0u; i < 40; i++)
        rec = controller.measure(sampleThroughput(rec, modelThroughput(rec, 25'000'000, link)));

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
        rec = controller.measure(sampleThroughput(rec, bps));
    }
    CHECK(settledBps / settledEpochs >= 0.9 * static_cast<double>(capped));
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_requests_before_threads") {
    auto controller = makeController();
    auto seed = controller.current();
    // Seeded threads can saturate the link at maximum requests
    uint64_t perRequest = 25'000'000;
    uint64_t link = static_cast<uint64_t>(seed.threads) * maxRequests * perRequest;
    auto rec = seed;
    for (auto i = 0u; i < 60; i++) {
        rec = controller.measure(sampleThroughput(rec, modelCeiling(rec, link, link, perRequest)));
        CHECK(rec.threads <= seed.threads + 1);
    }
    CHECK(rec.requestsPerThread == 32);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_requests_shrink_under_contention") {
    auto controller = makeController();
    auto seed = controller.current();
    // TLS model: thread-bound with contention above the knee
    uint64_t link = 36'250'000'000;
    auto rec = seed;
    for (auto i = 0u; i < 200; i++)
        rec = controller.measure(sampleThroughput(rec, modelCeiling(rec, link, 950'000'000, 76'250'000, 480)));
    // The knee caps the concurrency, so growing the requests past the seed never pays
    CHECK(rec.requestsPerThread <= seed.requestsPerThread);
    CHECK(rec.threads >= 12);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_no_ratchet_under_noise") {
    auto controller = makeController();
    uint64_t link = 2'000'000'000;
    auto rec = controller.current();
    // Converge first
    for (auto i = 0u; i < 40; i++)
        rec = controller.measure(sampleThroughput(rec, modelThroughput(rec, 25'000'000, link)));

    // Use the median to tolerate probes while detecting sustained throughput loss
    std::vector<uint64_t> settled;
    for (auto i = 0u; i < 300; i++) {
        auto clean = modelThroughput(rec, 25'000'000, link);
        if (i > 100)
            settled.push_back(clean);
        auto jitter = 1.0 + ((i * 2654435761u) % 800) / 10000.0 - 0.04;
        rec = controller.measure(sampleThroughput(rec, static_cast<uint64_t>(static_cast<double>(clean) * jitter)));
    }
    std::sort(settled.begin(), settled.end());
    CHECK(static_cast<double>(settled[settled.size() / 2]) >= 0.95 * static_cast<double>(link));
    CHECK(static_cast<double>(modelThroughput(rec, 25'000'000, link)) >= 0.95 * static_cast<double>(link));
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_sheds_below_the_seed_on_a_capped_link") {
    auto controller = makeController();
    auto seed = controller.current();
    // Half the seeded concurrency saturates the link
    uint64_t perRequest = 25'000'000;
    uint64_t link = static_cast<uint64_t>(seed.threads) * seed.requestsPerThread * perRequest / 2;

    auto rec = seed;
    std::vector<AdaptiveController::Recommendation> epochs;
    for (auto i = 0u; i < 200; i++) {
        auto clean = modelThroughput(rec, perRequest, link);
        // Inject an early burst above the plateau
        auto reported = i == 1 ? static_cast<uint64_t>(static_cast<double>(clean) * 1.1) : clean;
        rec = controller.measure(sampleThroughput(rec, reported));
        epochs.push_back(rec);
    }
    auto settled = held(epochs);
    CHECK(settled.threads < seed.threads);
    CHECK(static_cast<double>(modelThroughput(settled, perRequest, link)) >= 0.95 * static_cast<double>(link));
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_requests_ceiling") {
    static constexpr auto ceiling = 8u;
    auto controller = makeController();
    uint64_t link = 1'000'000'000'000;
    auto rec = controller.current();
    std::vector<AdaptiveController::Recommendation> epochs;
    for (auto i = 0u; i < 80; i++) {
        // A consumer never runs more than its group allows
        rec.requestsPerThread = std::min(rec.requestsPerThread, ceiling);
        rec = controller.measure(sampleThroughput(rec, modelCeiling(rec, link, link, 25'000'000), ceiling));
        CHECK(rec.requestsPerThread <= ceiling);
        epochs.push_back(rec);
    }
    auto settled = held(epochs);
    CHECK(settled.requestsPerThread == ceiling);
    CHECK(settled.threads > controller.maxThreads() / 8);
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
            rec = controller.measure(sampleThroughput(rec, static_cast<uint64_t>(static_cast<double>(clean) * jitter)));
            if (rec.threads <= target)
                return i;
        }
        return budget;
    };

    CHECK(epochsToReach(hardwareThreads, hardwareThreads / 2, 400) < 100);

    // Both seeds should converge under noise
    auto settle = [&](unsigned seed) {
        auto controller = makeController(seed);
        auto rec = controller.current();
        std::vector<AdaptiveController::Recommendation> epochs;
        for (auto i = 0u; i < 400; i++) {
            auto clean = modelCeiling(rec, link, threadBps, requestBps);
            auto jitter = 1.0 + ((i * 2654435761u) % 800) / 10000.0 - 0.04;
            rec = controller.measure(sampleThroughput(rec, static_cast<uint64_t>(static_cast<double>(clean) * jitter)));
            epochs.push_back(rec);
        }
        return held(epochs);
    };
    auto low = settle(1);
    auto high = settle(hardwareThreads);
    CHECK(high.threads <= low.threads + 4);
    CHECK(modelCeiling(high, link, threadBps, requestBps) >= link - link / 10);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_trades_threads_for_requests") {
    // Request concurrency limits throughput
    uint64_t link = 4'000'000'000;
    uint64_t threadBps = 4'000'000'000;
    uint64_t requestBps = link / 480;

    auto controller = makeController(hardwareThreads);
    auto seed = controller.current();
    auto rec = seed;
    for (auto i = 0u; i < 300; i++)
        rec = controller.measure(sampleThroughput(rec, modelCeiling(rec, link, threadBps, requestBps)));

    // The same concurrency is carried by fewer threads, and the link still saturates
    CHECK(rec.threads < seed.threads);
    CHECK(rec.requestsPerThread > seed.requestsPerThread);
    CHECK(modelCeiling(rec, link, threadBps, requestBps) >= link - link / 10);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_seed_from_config") {
    // The seeded threads of a controller on the given instance
    auto seedOf = [](uint64_t network, bool tls, unsigned hardware) {
        return AdaptiveController(modelConfig(network), tls, hardware).current().threads;
    };

    // Measured HTTP/TLS optima: 16/44 threads at 300 Gbit/s, 4/12 at 50 Gbit/s
    CHECK(seedOf(300'000, false, 96) == 19);
    CHECK(seedOf(300'000, true, 96) == 38);
    CHECK(seedOf(50'000, false, 36) == 4);
    CHECK(seedOf(50'000, true, 36) == 7);

    // Unknown bandwidth uses hardware concurrency
    CHECK(seedOf(0, true, 96) == 12);
    CHECK(seedOf(0, false, 4) == 1);

    CHECK(seedOf(600'000, true, 8) == 8);
    CHECK(seedOf(1'000, false, 8) == 1);

    // The seed applies up to the hardware bound
    auto seeded = makeController(19);
    CHECK(seeded.current().threads == 19);
    CHECK(seeded.current().requestsPerThread == Config::defaultCoreConcurrency);
    auto capped = makeController(2 * hardwareThreads);
    CHECK(capped.current().threads == hardwareThreads);
    auto unseeded = makeController();
    CHECK(unseeded.current().threads == hardwareThreads / 8);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_group_counters") {
    TaskedSendReceiverGroup group;
    CHECK(TaskedSendReceiverGroup::maxConcurrentRequests == 128);
    group.setConcurrentRequests(10);
    CHECK(group.getConcurrentRequests() == 10);
    group.setConcurrentRequests(32);
    CHECK(group.getConcurrentRequests() == 32);
    CHECK(group.getTransferredBytes() == 0);
    CHECK(group.getQueuedMessages() == 0);
    CHECK(group.getInflightMessages() == 0);
}
//---------------------------------------------------------------------------
} // namespace anyblob::network::test
