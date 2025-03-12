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
MessageResult::MessageResult() : response(), originError(nullptr), failureCode(), state(MessageState::Init)
// The default constructor
{
    dataVector = make_unique<utils::DataVector<uint8_t>>();
}
//---------------------------------------------------------------------------
MessageResult::MessageResult(uint8_t* data, uint64_t size) : response(), originError(nullptr), failureCode(), state(MessageState::Init)
// The constructor with buffer input
{
    if (data)
        dataVector = make_unique<utils::DataVector<uint8_t>>(data, size);
    else
        dataVector = make_unique<utils::DataVector<uint8_t>>();
}
//---------------------------------------------------------------------------
MessageResult::MessageResult(utils::DataVector<uint8_t>* dataVector) : response(), originError(nullptr), failureCode(), state(MessageState::Init)
// The constructor with buffer input
{
    this->dataVector = unique_ptr<utils::DataVector<uint8_t>>(dataVector);
}
//---------------------------------------------------------------------------
const string_view MessageResult::getResult() const
/// Get the result
{
    return string_view(reinterpret_cast<const char*>(dataVector->cdata()) + response->headerLength, response->length);
}
//---------------------------------------------------------------------------
string_view MessageResult::getResult()
/// Get the result
{
    return string_view(reinterpret_cast<char*>(dataVector->data()) + response->headerLength, response->length);
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
    return response->length;
}
//---------------------------------------------------------------------------
uint64_t MessageResult::getOffset() const
// Get the offset
{
    return response->headerLength;
}
//---------------------------------------------------------------------------
MessageState MessageResult::getState() const
// Get the state
{
    return state;
}
//---------------------------------------------------------------------------
uint16_t MessageResult::getFailureCode() const
// Get the failure code
{
    if (originError)
        return originError->getFailureCode();
    else
        return failureCode;
}
//---------------------------------------------------------------------------
std::string_view MessageResult::getResponseCode() const
// Get the response code
{
    if (originError)
        return originError->getResponseCode();
    else
        return HttpResponse::getResponseCode(response->response.code);
}
//---------------------------------------------------------------------------
uint64_t MessageResult::getResponseCodeNumber() const
// Get the response code number
{
    if (originError)
        return originError->getResponseCodeNumber();
    else if (response)
        return HttpResponse::getResponseCodeNumber(response->response.code);
    return 0;
}
//---------------------------------------------------------------------------
std::string_view MessageResult::getErrorResponse() const
// Get the error header
{
    if (originError)
        return originError->getErrorResponse();
    else if (response)
        return string_view(reinterpret_cast<char*>(dataVector->data()), dataVector->size());
    return ""sv;
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
