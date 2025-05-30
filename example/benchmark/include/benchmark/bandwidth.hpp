#pragma once
#include "cloud/provider.hpp"
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::benchmark {
class Bandwidth {
    public:
    /// Different bandwith systems
    enum class Systems : uint8_t {
        Uring,
        S3,
        S3Crt
    };
    /// Settings for the bandwidth benchmark
    struct Settings {
        /// Number of network sockets/threads
        uint32_t concurrentThreads;
        /// Number of simultaneous requests per socket
        uint32_t concurrentRequests;
        /// Total number of requests
        uint64_t requests;
        /// The chunksize
        uint64_t chunkSize = 64u * 1024;
        /// Choose random file from xxx.bin (xxx files from 0 to this)
        uint32_t blobFiles;
        /// The number of iterations
        uint32_t iterations = 1;
        /// The filepath
        std::string filePath;
        /// The report filename
        std::string report;
        /// The resolver
        std::string resolver = "throughput";
        /// The account / client email
        std::string account;
        /// The rsa key file
        std::string rsaKeyFile;
        /// The systems
        std::vector<Systems> systems;
        /// HTTPS
        bool https = false;
        /// Encryption at rest
        bool encryption = false;
        /// Test upload
        bool testUpload = false;
    };

    /// Benchmark runner
    static void run(const Settings& benchmarkSettings, const std::string& uri);
    /// Uring benchmark runner
    static void runUring(const Settings& benchmarkSettings, const std::string& uri);
    /// S3 benchmark runner
    template <typename S3SendReceiver>
    static void runS3(const Settings& benchmarkSettings, const std::string& uri);
    /// Timestamp util
    static inline size_t systemClockToMys(const std::chrono::steady_clock::time_point& t) {
        return std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count();
    }
};
//---------------------------------------------------------------------------
} // namespace anyblob::benchmark
