#pragma once
#include "loopback_proxy.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2026
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::test {
//---------------------------------------------------------------------------
/// This class relays tcp connections to a target on its own threads and injects a network fault into them
class FaultProxy : public LoopbackProxy {
    public:
    /// The injected fault
    enum class Mode : uint8_t {
        /// Reset right after the accept
        rstOnConnect,
        /// Accept and never answer
        acceptHang,
        /// Answer with bytes that are not http
        garbage,
        /// Forward arg bytes, then stop answering
        headerStall,
        /// Forward arg bytes, then close
        closeMidBody,
        /// Forward arg bytes, then reset
        rstMidBody,
        /// Delay the answer by arg milliseconds
        responseDelay,
        /// Pace the answer at arg bytes per second
        trickle,
        /// Reset the target after arg uploaded bytes
        rstMidUpload,
        /// Stall after arg uploaded bytes
        uploadStall,
        /// Corrupt the stream after arg bytes
        corruptStream,
        /// Close after arg milliseconds without traffic
        idleClose,
        /// Answer with a bad gateway response
        gatewayError,
        /// Answer with a slow down, arg names the retry after seconds
        throttle
    };

    private:
    /// The write size of the paced answer
    static constexpr uint64_t pieceSize = 1024;

    /// The fault
    Mode _mode;
    /// The fault parameter in bytes, milliseconds, or bytes per second
    uint64_t _arg;
    /// Fault only the first n connections, negative faults all
    int _faults;
    /// The accept time mutex
    mutable std::mutex _acceptMutex;
    /// The accept times
    std::vector<std::chrono::steady_clock::time_point> _acceptTimes;

    public:
    /// The constructor listens on a free loopback port and starts the proxy
    FaultProxy(const std::string& target, Mode mode, uint64_t arg = 0, int faults = -1);
    /// The destructor stops the proxy and closes all sockets
    ~FaultProxy();

    /// Get the name of a fault for the test output
    [[nodiscard]] static std::string_view getName(Mode mode);
    /// Get the accept times
    [[nodiscard]] std::vector<std::chrono::steady_clock::time_point> getAcceptTimes() const;

    private:
    /// Relay one connection and inject the faults of the handshake
    void relay(int client, uint64_t index);
    /// Forward one direction and inject the faults of the transfer
    void pump(int from, int to, bool faulty, bool downstream, std::atomic<uint64_t>& activity);
};
//---------------------------------------------------------------------------
} // namespace anyblob::test
