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
/// Fixed hardware model for reproducible tests
static constexpr auto hardwareThreads = 24u;
//---------------------------------------------------------------------------
/// Request limit per thread in the test model
static constexpr auto maxRequests = 32u;
//---------------------------------------------------------------------------
/// Model config; network bandwidth seeds the thread count
static Config modelConfig(uint64_t network) {
    return Config{Config::defaultCoreThroughput, Config::defaultCoreConcurrency, network};
}
//---------------------------------------------------------------------------
/// A controller on the modeled machine
static AdaptiveController makeController(unsigned seed = 0) {
    return AdaptiveController(modelConfig(2 * seed * Config::defaultCoreThroughput), false, hardwareThreads);
}
//---------------------------------------------------------------------------
/// One second with the recommendation applied
static AdaptiveController::Sample sampleThroughput(const AdaptiveController::Recommendation& applied, uint64_t bytesPerSec, unsigned ceiling = maxRequests) {
    return {bytesPerSec, std::chrono::nanoseconds(1s), 1000, 1000, applied, ceiling};
}
//---------------------------------------------------------------------------
/// One second without transfers or queued requests
static AdaptiveController::Sample sampleIdle(const AdaptiveController::Recommendation& applied, uint64_t inflight = 0) {
    return {0, std::chrono::nanoseconds(1s), 0, inflight, applied, maxRequests};
}
//---------------------------------------------------------------------------
/// Most frequent recent configuration, excluding occasional probes
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

    // Initial thread count for the instance
    auto seedOf = [](uint64_t network, bool tls, unsigned hardware) {
        return AdaptiveController(modelConfig(network), tls, hardware).current().threads;
    };

    // Higher bandwidth and TLS need more threads
    CHECK(seedOf(300'000, false, 96) > seedOf(50'000, false, 96));
    CHECK(seedOf(300'000, true, 96) > seedOf(300'000, false, 96));

    // Cap threads at hardware capacity; use a fallback for unknown bandwidth
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
    // More requests cannot help a CPU-bound workload
    auto settled = held(epochs);
    CHECK(settled.requestsPerThread <= seed.requestsPerThread);
    CHECK(settled.threads >= 2 * seed.threads);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_noise_does_not_shed_throughput") {
    auto controller = makeController();
    uint64_t perRequest = 25'000'000;
    uint64_t link = 1'000'000'000;

    // Model throughput loss when concurrency drops
    auto rec = controller.current();
    double delivered = 0;
    auto epochs = 0u;
    for (auto i = 0u; i < 200; i++) {
        auto clean = static_cast<double>(modelThroughput(rec, perRequest, link));
        // Alternate throughput by +/-5%
        auto noisy = static_cast<uint64_t>(clean * (i % 2 ? 1.05 : 0.95));
        rec = controller.measure(sampleThroughput(rec, noisy));
        if (i > 3 * AdaptiveController::probeInterval) {
            delivered += static_cast<double>(modelThroughput(rec, perRequest, link));
            epochs++;
        }
    }
    // Probes may dip, but average throughput should stay near capacity
    CHECK(delivered / epochs >= 0.9 * static_cast<double>(link));
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_controller_idle_and_reattach") {
    auto controller = makeController();
    auto seed = controller.current();
    uint64_t perRequest = 25'000'000;
    uint64_t link = 1'000'000'000'000;

    // In-flight requests keep the group active
    CHECK(controller.measure(sampleIdle(seed, 5)).threads == seed.threads);

    // Grow and record the settled configuration
    auto rec = controller.current();
    std::vector<AdaptiveController::Recommendation> epochs;
    for (auto i = 0u; i < 120; i++) {
        rec = controller.measure(sampleThroughput(rec, modelThroughput(rec, perRequest, link)));
        epochs.push_back(rec);
    }
    auto grown = held(epochs);
    CHECK(grown != seed);

    // Idle groups fall back to one thread
    rec = controller.measure(sampleIdle(grown));
    CHECK(rec.threads == 1);
    CHECK(rec.requestsPerThread == grown.requestsPerThread);

    // New demand restores the kept configuration
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

    // Recover link throughput after the per-request rate drops
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
        // Apply the group request limit
        rec.requestsPerThread = std::min(rec.requestsPerThread, ceiling);
        rec = controller.measure(sampleThroughput(rec, modelCeiling(rec, link, link, 25'000'000), ceiling));
        CHECK(rec.requestsPerThread <= ceiling);
        epochs.push_back(rec);
    }
    // Add threads when requests per thread are capped
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

    // Fewer threads still saturate the link
    CHECK(rec.threads < seed.threads);
    CHECK(rec.requestsPerThread > seed.requestsPerThread);
    CHECK(modelCeiling(rec, link, threadBps, requestBps) >= link - link / 10);
}
//---------------------------------------------------------------------------
} // namespace anyblob::network::test
