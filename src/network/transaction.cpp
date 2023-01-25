#include "network/transaction.hpp"
#include "network/original_message.hpp"
#include "network/tasked_send_receiver.hpp"
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
void Transaction::processSync(TaskedSendReceiver& sendReceiver)
// Processes the request messages
{
    // send the original request message
    for (auto& msg : messages) {
        sendReceiver.sendSync(msg.get());
    }

    // do the download work
    sendReceiver.processSync();
}
//---------------------------------------------------------------------------
void Transaction::processAsync(TaskedSendReceiver& sendReceiver)
// Sends the request messages to the task group
{
    // send the original request message
    for (auto& msg : messages) {
        sendReceiver.send(msg.get());
    }
}
//---------------------------------------------------------------------------
Transaction::iterator Transaction::begin()
/// The begin it
{
    return iterator(messages.begin());
}
//---------------------------------------------------------------------------
Transaction::iterator Transaction::end()
/// The end it
{
    return iterator(messages.end());
}
//---------------------------------------------------------------------------
Transaction::const_iterator Transaction::cbegin() const
/// The begin it
{
    return const_iterator(messages.cbegin());
}
//---------------------------------------------------------------------------
Transaction::const_iterator Transaction::cend() const
/// The end it
{
    return const_iterator(messages.cend());
}
//---------------------------------------------------------------------------
Transaction::Iterator::Iterator(const Transaction::message_vector_type::iterator& it)
// Constructor with iterator
{
    this->it = it;
}
//---------------------------------------------------------------------------
Transaction::Iterator::Iterator(const Transaction::Iterator& it) // Copy constructor
{
    this->it = it.it;
}
//---------------------------------------------------------------------------
Transaction::Iterator::reference Transaction::Iterator::operator*() const
// Reference
{
    return (*it)->result;
}
//---------------------------------------------------------------------------
Transaction::Iterator::pointer Transaction::Iterator::operator->() const
// Pointer
{
    return &(*it)->result;
}
//---------------------------------------------------------------------------
Transaction::Iterator& Transaction::Iterator::operator++()
// Inc
{
    ++it;
    return *this;
}
//---------------------------------------------------------------------------
Transaction::Iterator Transaction::Iterator::operator++(int)
// Post-inc
{
    Iterator prv(*this);
    operator++();
    return prv;
}
//---------------------------------------------------------------------------
bool Transaction::Iterator::operator==(const Transaction::Iterator& other) const
// Equality
{
    return it == other.it;
}
//---------------------------------------------------------------------------
Transaction::ConstIterator::ConstIterator(const Transaction::message_vector_type::const_iterator& it)
// Constructor with iterator
{
    this->it = it;
}
//---------------------------------------------------------------------------
Transaction::ConstIterator::ConstIterator(const Transaction::ConstIterator& it) // Copy constructor
{
    this->it = it.it;
}
//---------------------------------------------------------------------------
Transaction::ConstIterator::reference Transaction::ConstIterator::operator*() const
// Reference
{
    return (*it)->result;
}
//---------------------------------------------------------------------------
Transaction::ConstIterator::pointer Transaction::ConstIterator::operator->() const
// Pointer
{
    return &(*it)->result;
}
//---------------------------------------------------------------------------
Transaction::ConstIterator& Transaction::ConstIterator::operator++()
// Inc
{
    ++it;
    return *this;
}
//---------------------------------------------------------------------------
Transaction::ConstIterator Transaction::ConstIterator::operator++(int)
// Post-inc
{
    ConstIterator prv(*this);
    operator++();
    return prv;
}
//---------------------------------------------------------------------------
bool Transaction::ConstIterator::operator==(const Transaction::ConstIterator& other) const
// Equality
{
    return it == other.it;
}
//---------------------------------------------------------------------------
}; // namespace network
}; // namespace anyblob
