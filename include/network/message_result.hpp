#pragma once
#include <atomic>
#include <memory>
#include <string_view>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2023
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace utils {
//---------------------------------------------------------------------------
template <typename T>
class DataVector;
//---------------------------------------------------------------------------
} // namespace utils
//---------------------------------------------------------------------------
namespace network {
//---------------------------------------------------------------------------
struct OriginalMessage;
struct HTTPMessage;
//---------------------------------------------------------------------------
/// Current status of the message
enum class MessageState : uint8_t {
    Init,
    InitSending,
    Sending,
    InitReceiving,
    Receiving,
    Finished,
    Aborted
};
//---------------------------------------------------------------------------
/// The result class
class MessageResult {
    protected:
    /// The data
    std::unique_ptr<utils::DataVector<uint8_t>> dataVector;
    /// The size of the result; the size of the message body
    uint64_t size;
    /// The offset of the result; the start after the message header
    uint64_t offset;
    /// The state
    std::atomic<MessageState> state;

    public:
    /// The default constructor
    MessageResult();
    /// The constructor with buffer input
    MessageResult(uint8_t* data, uint64_t size);
    /// The constructor with buffer input
    MessageResult(utils::DataVector<uint8_t>* dataVector);

    /// Get the result
    const std::string_view getResult() const;
    /// Get the result
    std::string_view getResult();
    /// Get the const data
    const uint8_t* getData() const;
    /// Get the data
    uint8_t* getData();
    /// Transfer owenership
    std::unique_ptr<uint8_t[]> moveData();
    /// Get the size
    uint64_t getSize() const;
    /// Get the offset
    uint64_t getOffset() const;
    /// Get the state
    MessageState getState() const;
    /// Is the data owned by this object
    bool owned() const;
    /// Was the request successful
    bool success() const;

    /// Get the data vector reference
    utils::DataVector<uint8_t>& getDataVector();
    /// Transfer owenership of data vector
    std::unique_ptr<utils::DataVector<uint8_t>> moveDataVector();

    /// Define the friend message and message tasks
    friend HTTPMessage;
    friend OriginalMessage;
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
