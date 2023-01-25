#pragma once
#include "cloud/provider.hpp"
#include "network/original_message.hpp"
#include "network/transaction.hpp"
#include <cassert>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2023
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
//---------------------------------------------------------------------------
namespace network {
//---------------------------------------------------------------------------
/// Specialization for get requests
class GetTransaction : public Transaction {
    public:
    /// The default constructor
    GetTransaction() = default;
    /// The explicit constructor
    explicit GetTransaction(const cloud::Provider* provider) : Transaction(provider) {}

    /// Build a new get request for synchronous calls
    void addRequest(const std::string& remotePath, std::pair<uint64_t, uint64_t> range = {0, 0}, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        assert(provider);
        auto originalMsg = std::make_unique<network::OriginalMessage>(provider->getRequest(remotePath, range), provider->getAddress(), provider->getPort(), result, capacity, traceId);
        messages.push_back(std::move(originalMsg));
    }

    /// Build a new get request with callback
    template <typename Callback>
    void addRequest(Callback&& callback, const std::string& remotePath, std::pair<uint64_t, uint64_t> range = {0, 0}, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        assert(provider);
        auto originalMsg = std::make_unique<network::OriginalCallbackMessage<Callback>>(std::forward<Callback&&>(callback), provider->getRequest(remotePath, range), provider->getAddress(), provider->getPort(), result, capacity, traceId);
        messages.push_back(std::move(originalMsg));
    }
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
