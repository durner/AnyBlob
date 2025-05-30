#pragma once
#include "cloud/provider.hpp"
#include <vector>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::cloud {
//---------------------------------------------------------------------------
/// Implements the Azure instances
struct AzureInstance : public Provider::Instance {
    /// Gets a vector of instance type infos
    [[nodiscard]] static std::vector<AzureInstance> getInstanceDetails();
};
//---------------------------------------------------------------------------
} // namespace anyblob::cloud
