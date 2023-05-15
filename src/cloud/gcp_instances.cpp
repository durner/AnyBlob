#include "cloud/gcp_instances.hpp"
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
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
vector<GCPInstance> GCPInstance::getInstanceDetails()
// Gets a vector of instance type infos
{
    // TODO: add instances
    vector<GCPInstance> instances = {
        {"n2-standard-2", 8, 2, "10"},
        {"n2-standard-4", 16, 4, "10"},
        {"n2-standard-8", 32, 8, "16"},
        {"n2-standard-16", 64, 16, "32"},
        {"n2-standard-32", 128, 32, "50"},
        {"n2-standard-48", 192, 48, "50"},
        {"n2-standard-64", 256, 64, "75"},
        {"n2-standard-80", 320, 80, "100"}};
    return instances;
}
//---------------------------------------------------------------------------
}; // namespace cloud
}; // namespace anyblob
