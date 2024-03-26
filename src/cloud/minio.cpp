#include "cloud/minio.hpp"
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
Provider::Instance MinIO::getInstanceDetails(network::TaskedSendReceiverHandle& /*sendReceiver*/)
// No real information for MinIO
{
    return AWSInstance{"minio", 0, 0, 0};
}
//---------------------------------------------------------------------------
string MinIO::getAddress() const
// Gets the address of MinIO
{
    // MinIO does not support virtual-hosted adresses, thus we use path-style requests
    assert(!_settings.endpoint.empty());
    return _settings.endpoint;
}
//---------------------------------------------------------------------------
} // namespace cloud
} // namespace anyblob
