#include "network/message_result.hpp"
#include "utils/data_vector.hpp"
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2023
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace network {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
MessageResult::MessageResult() : size(), offset(), failureCode(), state(MessageState::Init)
// The default constructor
{
    dataVector = make_unique<utils::DataVector<uint8_t>>();
}
//---------------------------------------------------------------------------
MessageResult::MessageResult(uint8_t* data, uint64_t size) : size(), offset(), failureCode(), state(MessageState::Init)
// The constructor with buffer input
{
    if (data)
        dataVector = make_unique<utils::DataVector<uint8_t>>(data, size);
    else
        dataVector = make_unique<utils::DataVector<uint8_t>>();
}
//---------------------------------------------------------------------------
MessageResult::MessageResult(utils::DataVector<uint8_t>* dataVector) : size(), offset(), failureCode(), state(MessageState::Init)
// The constructor with buffer input
{
    this->dataVector = unique_ptr<utils::DataVector<uint8_t>>(dataVector);
}
//---------------------------------------------------------------------------
const string_view MessageResult::getResult() const
/// Get the result
{
    return string_view(reinterpret_cast<const char*>(dataVector->cdata()) + offset, size);
}
//---------------------------------------------------------------------------
string_view MessageResult::getResult()
/// Get the result
{
    return string_view(reinterpret_cast<char*>(dataVector->data()) + offset, size);
}
//---------------------------------------------------------------------------
const uint8_t* MessageResult::getData() const
// Get the const data
{
    return dataVector->cdata();
}
//---------------------------------------------------------------------------
uint8_t* MessageResult::getData()
// Get the data
{
    return dataVector->data();
}
//---------------------------------------------------------------------------
unique_ptr<uint8_t[]> MessageResult::moveData()
// Transfer owenership
{
    return dataVector->transferBuffer();
}
//---------------------------------------------------------------------------
uint64_t MessageResult::getSize() const
// Get the size
{
    return size;
}
//---------------------------------------------------------------------------
uint64_t MessageResult::getOffset() const
// Get the offset
{
    return offset;
}
//---------------------------------------------------------------------------
MessageState MessageResult::getState() const
// Get the state
{
    return state;
}
//---------------------------------------------------------------------------
uint16_t MessageResult::getFailureCode() const
// Get the original message
{
    return failureCode;
}
//---------------------------------------------------------------------------
bool MessageResult::owned() const
// Is the data onwed by this
{
    return dataVector->owned();
}
//---------------------------------------------------------------------------
bool MessageResult::success() const
// Was the request successful
{
    return state == MessageState::Finished;
}
//---------------------------------------------------------------------------
utils::DataVector<uint8_t>& MessageResult::getDataVector()
// Returns the datavector as reference
{
    return *dataVector;
}
//---------------------------------------------------------------------------
unique_ptr<utils::DataVector<uint8_t>> MessageResult::moveDataVector()
// Moves the data vector
{
    return move(dataVector);
}
//---------------------------------------------------------------------------
}; // namespace network
}; // namespace anyblob
