#pragma once
#include <memory>
#include <string>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
//---------------------------------------------------------------------------
namespace network {
class OriginalMessage;
}; // namespace network
//---------------------------------------------------------------------------
namespace cloud {
//---------------------------------------------------------------------------
/// The blob handler used to transfer data from remote storage
/// Rewrites the DataVector<uint8_t> to data, capacity and size
/// to reduce dependencies between AnyBlob and external callers
struct Blob {
    /// The path to be accessed
    std::string remotePath;
    /// The range to be accessed
    std::pair<uint64_t, uint64_t> range;

    /// The original message
    std::unique_ptr<anyblob::network::OriginalMessage> originalMsg;

    /// The data buffer pointer which needs to hold data + transfer data (e.g. HTTP Header)
    std::unique_ptr<uint8_t[]> data;
    /// The maximum data buffer space capacity
    uint64_t capacity;

    /// The data offset (e.g. the real data offset after the http header)
    uint64_t offset;
    /// The actual used size of data (e.g. the real data size without transfer header (http header)
    uint64_t size;

    /// The constructor
    Blob();
    /// The destructor
    ~Blob();
};
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
