#pragma once
#include "cloud/aws.hpp"
#include "network/original_message.hpp"
#include "utils/data_vector.hpp"
#include "utils/ring_buffer.hpp"
#include "utils/timer.hpp"
#include "utils/unordered_map.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <aws/core/Aws.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/utils/Outcome.h>
#include <aws/core/utils/stream/PreallocatedStreamBuf.h>
#include <aws/s3-crt/S3CrtClient.h>
#include <aws/s3-crt/model/GetObjectRequest.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <unistd.h>
// ---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace network {
//---------------------------------------------------------------------------
/// Implements a send recieve roundtrip with the help of AWS S3 Library
/// S3SendReceiver contains mutliple requests
template <typename TS3Client, typename TGetObjectRequest, typename TGetObjectResponseReceivedHandler, typename TGetObjectOutcome, typename TClientConfiguration>
class S3SendReceiver {
    public:
    /// Public Typedefs
    typedef TS3Client S3Client;
    typedef TGetObjectRequest GetObjectRequest;
    typedef TGetObjectResponseReceivedHandler GetObjectResponseReceivedHandler;
    typedef TGetObjectOutcome GetObjectOutcome;
    typedef TClientConfiguration ClientConfiguration;

    /// The message which discards the file
    struct GetObjectRequestMessage {
        public:
        /// The request
        GetObjectRequest objectRequest;
        /// The result data
        std::unique_ptr<utils::DataVector<uint8_t>> data;
        /// The response handler
        std::unique_ptr<GetObjectResponseReceivedHandler> responseHandler;
        /// The output IO stream for the request (optional)
        std::unique_ptr<Aws::Utils::Stream::PreallocatedStreamBuf> streamBuf;

        /// The constructor
        GetObjectRequestMessage(GetObjectRequest req) : objectRequest(req), data(), responseHandler(), streamBuf() {}

        /// The destructor
        virtual ~GetObjectRequestMessage() = default;

        /// Callback required
        virtual bool requiresFinish() { return false; }

        /// Callback
        virtual void finish() {}
    };

    /// The callback message
    template <typename Callback>
    struct GetObjectRequestCallbackMessage : public GetObjectRequestMessage {
        /// The callback
        Callback callback;

        /// The constructor
        GetObjectRequestCallbackMessage(Callback callback, GetObjectRequest req) : GetObjectRequestMessage(req), callback(callback) {}

        /// The destructor
        virtual ~GetObjectRequestCallbackMessage() override = default;

        /// Override if callback required
        bool requiresFinish() override { return true; }

        void finish() override {
            callback(move(GetObjectRequestMessage::data));
        }
    };

    private:
    /// The submission buffer
    utils::RingBuffer<GetObjectRequestMessage*> _submissions;
    /// The completed requests
    utils::UnorderedMap<uintptr_t, std::unique_ptr<utils::DataVector<uint8_t>>> _completions;
    /// The timinings info
    std::vector<utils::TimingHelper>* _timings;
    /// Reuse elements
    utils::RingBuffer<utils::DataVector<uint8_t>*> _reuse;
    /// The S3 client
    std::unique_ptr<S3Client> _client;
    /// The s3 client config
    std::unique_ptr<ClientConfiguration> _config;
    /// Current requests in flight
    std::atomic<uint64_t> _currentSubmissions;
    /// Total submissions
    std::atomic<uint64_t> _totalSubmission;
    /// The queue maximum
    uint64_t _concurrentRequests;
    /// Condition variable to stop wasting wait cycles
    std::condition_variable _cv;
    /// Mutex for condition variable
    std::mutex _mutex;
    /// Stops the daemon
    std::atomic<bool> _stopDeamon;

    public:
    /// The constructor
    S3SendReceiver(uint64_t submissions, uint64_t concurrentRequests, const std::string& region, uint64_t threadsOrThroughput = 1, bool https = false) : _submissions(submissions), _completions(0.2 * submissions), _timings(nullptr), _reuse(submissions), _currentSubmissions(), _totalSubmission(), _concurrentRequests(concurrentRequests), _stopDeamon(false)
    /// The constructor
    {
        _config = std::make_unique<ClientConfiguration>();
        _config->region = region;
        if (https)
            _config->scheme = Aws::Http::Scheme::HTTPS;
        else
            _config->scheme = Aws::Http::Scheme::HTTP;

        if constexpr (std::is_same_v<ClientConfiguration, Aws::Client::ClientConfiguration>) {
            _config->maxConnections = threadsOrThroughput;
            _config->verifySSL = false;
            _config->enableTcpKeepAlive = true;
        } else {
            _config->throughputTargetGbps = threadsOrThroughput;
        }

        _client = std::make_unique<S3Client>(*_config.get());
    }

    /// The destructor
    ~S3SendReceiver() {
        while (auto val = _reuse.consume<false>()) {
            if (val.has_value())
                delete val.value();
        }
    }

    /// Reuse message
    uint64_t reuse(std::unique_ptr<utils::DataVector<uint8_t>> message) {
        return _reuse.insert<false>(message.release()) != ~0ull;
    }

    /// Adds a message to the submission queue
    uint64_t send(GetObjectRequestMessage* msg) {
        return _submissions.insert(msg) != ~0ull;
    }

    /// Receives the messages and blocks until it is received
    std::unique_ptr<utils::DataVector<uint8_t>> receive(GetObjectRequestMessage* msg) {
        auto it = _completions.find(reinterpret_cast<uintptr_t>(msg));
        if (it != _completions.end()) {
            auto res = move(it->second);
            std::ignore = _completions.erase(it);
            return res;
        } else {
            return nullptr;
        }
    }

    /// Receives the messages if available else nullptr
    std::unique_ptr<utils::DataVector<uint8_t>> receiveAsync(GetObjectRequestMessage* msg) {
        auto it = _completions.find(reinterpret_cast<uintptr_t>(msg));
        if (it != _completions.end()) {
            auto res = move(it->second);
            std::ignore = _completions.erase(it);
            return res;
        } else {
            return nullptr;
        }
    }

    /// Set the timings
    void setTimings(std::vector<utils::TimingHelper>* timings) {
        _timings = timings;
    }

    /// Runs the deamon
    void run() {
        try {
            sendReceive();
        } catch (std::exception& e) {
            std::cout << e.what() << std::endl;
        };
    }

    /// Stops the deamon
    void stop() { _stopDeamon = true; }

    /// Creates a sending message and waits for result
    void sendReceive(bool oneQueueInvocation = false) {
        // Reset the deamon status
        _stopDeamon = false;

        // Send new requests
        auto emplaceNewRequest = [&] {
            while (auto val = _submissions.template consume<false>()) {
                /// Create new submission requests if within concurrency
                if (!val.has_value())
                    break;

                auto original = val.value();

                if (!original->streamBuf) {
                    if (auto mem = _reuse.consume<false>()) {
                        if (mem.has_value()) {
                            original->data = std::unique_ptr<utils::DataVector<uint8_t>>(mem.value());
                            original->streamBuf = std::make_unique<Aws::Utils::Stream::PreallocatedStreamBuf>(reinterpret_cast<unsigned char*>(original->data->data()), original->data->capacity());
                            original->objectRequest.SetResponseStreamFactory([original]() { return Aws::New<Aws::IOStream>("", original->streamBuf.get()); });
                        }
                    }
                }

                original->responseHandler = move(fetchObject(original, _currentSubmissions, _totalSubmission));

                if (_currentSubmissions >= _concurrentRequests)
                    break;
            }
        };

        // iterate over all messages
        while (!_stopDeamon) {
            if (_currentSubmissions < _concurrentRequests) {
                emplaceNewRequest();
            } else {
                std::unique_lock<std::mutex> lock(_mutex);
                _cv.wait(lock);
            }

            if (oneQueueInvocation && !_currentSubmissions) {
                _stopDeamon = true;
            }
        }
    }

    /// Fetches S3 Object async
    /// Requires memcpy from outcome.GetResult() to the data vector of req->finish() and _completions
    std::unique_ptr<GetObjectResponseReceivedHandler> fetchObject(GetObjectRequestMessage* req, std::atomic<uint64_t>& submissions, std::atomic<uint64_t>& totalSubmission) {
        uint64_t submissionId = 0;
        if (_timings) {
            submissionId = totalSubmission.fetch_add(1);
            (*_timings)[submissionId].start = std::chrono::steady_clock::now();
        }

        auto callback = std::make_unique<GetObjectResponseReceivedHandler>([&submissions, submissionId, req, this](const S3Client*, const GetObjectRequest&, const GetObjectOutcome& outcome, const std::shared_ptr<const Aws::Client::AsyncCallerContext>&) {
            if (outcome.IsSuccess()) {
                outcome.GetResult();
                req->data->resize(outcome.GetResult().GetContentLength());
                // https://github.com/aws/aws-sdk-cpp/issues/44
                if (req->requiresFinish())
                    req->finish();
                else
                    _completions.push(reinterpret_cast<uintptr_t>(&req), move(req->data));
                _cv.notify_all();
                if (_timings) {
                    (*_timings)[submissionId].size = outcome.GetResult().GetContentLength();
                    (*_timings)[submissionId].finish = std::chrono::steady_clock::now();
                }
                submissions--;
            }
        });
        submissions++;
        _client->GetObjectAsync(req->objectRequest, *callback.get());
        return callback;
    }

    /// Fetches S3 Object sync
    void getObjectSync(GetObjectRequestMessage* req, std::atomic<uint64_t>& submissions, std::atomic<uint64_t>& totalSubmission) {
        uint64_t submissionId = 0;
        if (_timings) {
            submissionId = totalSubmission.fetch_add(1);
            (*_timings)[submissionId].start = std::chrono::steady_clock::now();
        }
        submissions++;
        auto outcome = _client->GetObject(req->objectRequest);
        if (outcome.IsSuccess()) {
            req->data->resize(outcome.GetResult().GetContentLength());
            // https://github.com/aws/aws-sdk-cpp/issues/44
            if (req->requiresFinish())
                req->finish();
            else
                _completions.push(reinterpret_cast<uintptr_t>(&req), move(req->data));
            submissions--;
            _cv.notify_all();
            if (_timings) {
                (*_timings)[submissionId].size = outcome.GetResult().GetContentLength();
                (*_timings)[submissionId].finish = std::chrono::steady_clock::now();
            }
        }
    }
};
//---------------------------------------------------------------------------
typedef S3SendReceiver<Aws::S3::S3Client, Aws::S3::Model::GetObjectRequest, Aws::S3::GetObjectResponseReceivedHandler, Aws::S3::Model::GetObjectOutcome, Aws::Client::ClientConfiguration> S3CurlSendReceiver;
typedef S3SendReceiver<Aws::S3Crt::S3CrtClient, Aws::S3Crt::Model::GetObjectRequest, Aws::S3Crt::GetObjectResponseReceivedHandler, Aws::S3Crt::Model::GetObjectOutcome, Aws::S3Crt::ClientConfiguration> S3CrtSendReceiver;
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
