#include "cloud/ibm.hpp"
#include "cloud/ibm_instances.hpp"
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
namespace cloud {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
Provider::Instance IBM::getInstanceDetails(network::TaskedSendReceiver& /*sendReceiver*/)
// IBM instance info
{
    // TODO: add instances and retrieve VM information
    return IBMInstance{"ibm", 0, 0, ""};
}
//---------------------------------------------------------------------------
string IBM::getAddress() const
// Gets the address of IBM COS
{
    return "s3." + _settings.region + ".cloud-object-storage.appdomain.cloud";
}
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
