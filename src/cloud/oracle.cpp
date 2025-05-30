#include "cloud/oracle.hpp"
#include "cloud/oracle_instances.hpp"
#include <string>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::cloud {
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
Provider::Instance Oracle::getInstanceDetails(network::TaskedSendReceiverHandle& /*sendReceiver*/)
// Get the instance details
{
    // TODO: add instances and retrieve shape information
    return OracleInstance{"oracle", 0, 0, 0};
}
//---------------------------------------------------------------------------
string Oracle::getAddress() const
// Gets the address of the Oracle Cloud Storage
{
    return _settings.bucket + ".compat.objectstorage." + _settings.region + ".oraclecloud.com";
}
//---------------------------------------------------------------------------
} // namespace anyblob::cloud
