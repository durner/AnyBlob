#include "network/adaptive_controller.hpp"
#include "catch2/single_include/catch2/catch.hpp"
#include "network/config.hpp"
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
static AdaptiveController::Sample sampleThroughput(const AdaptiveController::Recommendation& applied, uint64_t bytesPerSec, unsigned ceiling = maxRequests) {
    return {bytesPerSec, std::chrono::nanoseconds(1s), 1000, 1000, applied, ceiling};
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
static uint64_t modelCeiling(const AdaptiveController::Recommendation& rec, uint64_t linkBps, uint64_t threadBps, uint64_t requestBps) {
    auto concurrency = static_cast<uint64_t>(rec.threads) * rec.requestsPerThread;
    return std::min({linkBps, threadBps * rec.threads, concurrency * requestBps});
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_seeding") {
    auto controller = makeController();
    CHECK(controller.current().threads == hardwareThreads / 8);
    CHECK(controller.current().requestsPerThread == Config::defaultCoreConcurrency);
    CHECK(controller.maxThreads() == hardwareThreads);

    // The seeded threads of a controller on the given instance
    auto seedOf = [](uint64_t network, bool tls, unsigned hardware) {
        return AdaptiveController(modelConfig(network), tls, hardware).current().threads;
    };

    // A wider link and the TLS cost both ask for more threads
    CHECK(seedOf(300'000, false, 96) > seedOf(50'000, false, 96));
    CHECK(seedOf(300'000, true, 96) > seedOf(300'000, false, 96));

    // The seed stays within the hardware bound, an unknown bandwidth falls back to it
    CHECK(seedOf(600'000, true, 8) == 8);
    CHECK(seedOf(0, true, 96) == 12);
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
        CHECK(rec.requestsPerThread <= maxRequests);
    }
    CHECK(settledBps / settledEpochs >= 0.9 * static_cast<double>(link));

    // The link saturates long before the hardware does
    CHECK(rec.threads < hardwareThreads / 2);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_cpu_bound_growth") {
    auto controller = makeController();
    auto seed = controller.current();

    // CPU-bound scaling
    auto rec = seed;
    std::vector<AdaptiveController::Recommendation> epochs;
    for (auto i = 0u; i < 120; i++) {
        auto bps = static_cast<uint64_t>(rec.threads) * 1'000'000'000;
        rec = controller.measure(sampleThroughput(rec, bps));
        epochs.push_back(rec);
    }
    // Requests never help a thread bound workload
    auto settled = held(epochs);
    CHECK(settled.requestsPerThread <= seed.requestsPerThread);
    CHECK(settled.threads >= 2 * seed.threads);
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
TEST_CASE("adaptive_controller_idle_and_reattach") {
    auto controller = makeController();
    auto seed = controller.current();
    uint64_t perRequest = 25'000'000;
    uint64_t link = 1'000'000'000'000;

    // Requests still in flight are not an idle group
    CHECK(controller.measure(sampleIdle(seed, 5)).threads == seed.threads);

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
TEST_CASE("adaptive_controller_degradation_recovery") {
    auto controller = makeController();
    uint64_t link = 2'000'000'000;

    // Converge to the plateau
    auto rec = controller.current();
    for (auto i = 0u; i < 12; i++)
        rec = controller.measure(sampleThroughput(rec, modelThroughput(rec, 25'000'000, link)));

    // The per-request throughput collapses, the controller has to buy the link back
    for (auto i = 0u; i < 60; i++)
        rec = controller.measure(sampleThroughput(rec, modelThroughput(rec, 5'000'000, link)));
    CHECK(static_cast<double>(modelThroughput(rec, 5'000'000, link)) >= 0.9 * static_cast<double>(link));
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
    CHECK(rec.requestsPerThread == maxRequests);
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
    // The requests are capped, so the threads have to carry the concurrency
    auto settled = held(epochs);
    CHECK(settled.requestsPerThread == ceiling);
    CHECK(settled.threads > controller.maxThreads() / 8);
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
} // namespace anyblob::network::test
