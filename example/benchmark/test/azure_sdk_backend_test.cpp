#include "benchmark/bandwidth.hpp"
#include <azure/core/http/http.hpp>
#include <azure/core/http/raw_response.hpp>
#include <azure/core/http/transport.hpp>
#include <azure/core/io/body_stream.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
class FakeTransport final : public Azure::Core::Http::HttpTransport {
    bool wrongRange;
    bool extraByte;
    std::vector<uint8_t> body;
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> active{0};
    std::atomic<uint64_t> maximum{0};
    std::atomic<bool> rangeValid{true};

    public:
    FakeTransport(bool wrongRangeValue = false, bool extraByteValue = false)
        : wrongRange(wrongRangeValue), extraByte(extraByteValue), body(extraByteValue ? 17 : 16, 42) {}

    std::unique_ptr<Azure::Core::Http::RawResponse> Send(
        Azure::Core::Http::Request& request,
        const Azure::Core::Context&) override {
        ++calls;
        const auto inFlight = active.fetch_add(1) + 1;
        auto observed = maximum.load();
        while (observed < inFlight && !maximum.compare_exchange_weak(observed, inFlight)) {
        }

        const auto headers = request.GetHeaders();
        const auto range = headers.find("x-ms-range");
        if (range == headers.end() || range->second != "bytes=7-22")
            rangeValid = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));

        auto response = std::make_unique<Azure::Core::Http::RawResponse>(
            1, 1, Azure::Core::Http::HttpStatusCode::PartialContent, "Partial Content");
        response->SetHeader("Content-Length", "16");
        response->SetHeader("Content-Range", wrongRange ? "bytes 8-23/100" : "bytes 7-22/100");
        response->SetHeader("x-ms-blob-type", "BlockBlob");
        response->SetHeader("x-ms-server-encrypted", "true");
        response->SetHeader("x-ms-creation-time", "Wed, 01 Jan 2025 00:00:00 GMT");
        response->SetBodyStream(std::make_unique<Azure::Core::IO::MemoryBodyStream>(body));
        --active;
        return response;
    }

    uint64_t getCalls() const { return calls; }
    uint64_t getMaximum() const { return maximum; }
    bool hasValidRanges() const { return rangeValid; }
};

anyblob::benchmark::Bandwidth::Settings settings(
    const std::shared_ptr<Azure::Core::Http::HttpTransport>& transport) {
    anyblob::benchmark::Bandwidth::Settings value;
    value.systems = {anyblob::benchmark::Bandwidth::Systems::AzureSdk};
    value.oneLake = true;
    value.https = true;
    value.objectPath = "lake.Lakehouse/Files/object";
    value.readOffset = 7;
    value.readLength = 16;
    value.requests = 4;
    value.concurrentThreads = 1;
    value.concurrentRequests = 4;
    value.requestTimeoutMs = 1000;
    value.azureTransport = transport;
    return value;
}

bool rejectsResponse(bool wrongRange, bool extraByte) {
    auto transport = std::make_shared<FakeTransport>(wrongRange, extraByte);
    try {
        anyblob::benchmark::Bandwidth::run(settings(transport), "azure://workspace/");
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}
} // namespace

int main() {
    if (setenv("AZURE_STORAGE_BEARER_TOKEN", "fake-token", 1) != 0)
        return 1;

    auto transport = std::make_shared<FakeTransport>();
    anyblob::benchmark::Bandwidth::run(settings(transport), "azure://workspace/");
    if (transport->getCalls() != 4 || transport->getMaximum() != 4 || !transport->hasValidRanges())
        return 2;
    if (!rejectsResponse(true, false))
        return 3;
    if (!rejectsResponse(false, true))
        return 4;

    unsetenv("AZURE_STORAGE_BEARER_TOKEN");
    std::cout << "Azure SDK backend fake-transport tests passed" << std::endl;
    return 0;
}
