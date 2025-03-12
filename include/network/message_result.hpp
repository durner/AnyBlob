#pragma once
#include "network/http_helper.hpp"
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
struct HTTPSMessage;
class TLSConnection;
class Transaction;
//---------------------------------------------------------------------------
/// Current status of the message
enum class MessageState : uint8_t {
    Init,
    TLSHandshake,
    InitSending,
    Sending,
    InitReceiving,
    Receiving,
    TLSShutdown,
    Finished,
    Aborted,
    Cancelled
};
//---------------------------------------------------------------------------
/// The failure codes
enum class MessageFailureCode : uint16_t {
    /// Socket creation error
    Socket = 1,
    /// Empty request error
    Empty = 1 << 1,
    /// Timeout passed
    Timeout = 1 << 2,
    /// Send syscall error
    Send = 1 << 3,
    /// Recv syscall error
    Recv = 1 << 4,
    /// HTTP header error
    HTTP = 1 << 5,
    /// TLS error
    TLS = 1 << 6
};
//---------------------------------------------------------------------------
/// The result class
class MessageResult {
    protected:
    /// The data
    std::unique_ptr<utils::DataVector<uint8_t>> dataVector;
    /// The http response header info
    std::unique_ptr<HttpHelper::Info> response;
    /// The error response
    const MessageResult* originError;
    /// The failure code
    uint16_t failureCode;
    /// The state
    std::atomic<MessageState> state;

    public:
    /// The default constructor
    MessageResult();
    /// The constructor with buffer input
    MessageResult(uint8_t* data, uint64_t size);
    /// The constructor with buffer input
    explicit MessageResult(utils::DataVector<uint8_t>* dataVector);

    /// Get the result
    [[nodiscard]] const std::string_view getResult() const;
    /// Get the result
    [[nodiscard]] std::string_view getResult();
    /// Get the const data
    [[nodiscard]] const uint8_t* getData() const;
    /// Get the data
    [[nodiscard]] uint8_t* getData();
    /// Transfer owenership
    [[nodiscard]] std::unique_ptr<uint8_t[]> moveData();
    /// Get the size
    [[nodiscard]] uint64_t getSize() const;
    /// Get the offset
    [[nodiscard]] uint64_t getOffset() const;
    /// Get the state
    [[nodiscard]] MessageState getState() const;
    /// Get the failure code
    [[nodiscard]] uint16_t getFailureCode() const;
    /// Get the error response (incl. header)
    [[nodiscard]] std::string_view getErrorResponse() const;
    /// Get the error response code
    [[nodiscard]] std::string_view getResponseCode() const;
    /// Get the error response code number
    [[nodiscard]] uint64_t getResponseCodeNumber() const;
    /// Is the data owned by this object
    [[nodiscard]] bool owned() const;
    /// Was the request successful
    [[nodiscard]] bool success() const;

    /// Get the data vector reference
    [[nodiscard]] utils::DataVector<uint8_t>& getDataVector();
    /// Transfer owenership of data vector
    [[nodiscard]] std::unique_ptr<utils::DataVector<uint8_t>> moveDataVector();

    /// Define the friend message and message tasks
    friend HTTPMessage;
    friend HTTPSMessage;
    friend OriginalMessage;
    friend TLSConnection;
    friend Transaction;
};
//---------------------------------------------------------------------------
} // namespace network
} // namespace anyblob
