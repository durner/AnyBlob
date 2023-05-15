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
/// Specialization of a delete request
class DeleteTransaction : public Transaction {
    public:
    /// The default constructor
    DeleteTransaction() = default;
    /// The explicit constructor
    explicit DeleteTransaction(const cloud::Provider* provider) : Transaction(provider) {}

    /// Build a new delete request for synchronous calls
    inline void addRequest(const std::string& remotePath, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        assert(provider);
        auto originalMsg = std::make_unique<network::OriginalMessage>(provider->deleteRequest(remotePath), provider->getAddress(), provider->getPort(), result, capacity, traceId);
        messages.push_back(std::move(originalMsg));
    }

    /// Build a new delete request with callback
    template <typename Callback>
    inline void addRequest(Callback&& callback, const std::string& remotePath, uint8_t* result = nullptr, uint64_t capacity = 0, uint64_t traceId = 0) {
        assert(provider);
        auto originalMsg = std::make_unique<network::OriginalCallbackMessage<Callback>>(std::forward<Callback&&>(callback), provider->deleteRequest(remotePath), provider->getAddress(), provider->getPort(), result, capacity, traceId);
        messages.push_back(std::move(originalMsg));
    }
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
