#include <cstring>
#include "cloud/aws.hpp"
#include "cloud/provider.hpp"
#include "network/transaction.hpp"
#include "network/tasked_send_receiver.hpp"
#include <iostream>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2022
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
int main(int /*argc*/, char** /*argv*/) {
    // The file to be downloaded
    auto bucketName = "s3://cloud-storage-analytics:eu-central-1";
    auto fileName = "tpchSf500.db/tpchSf500.db";

    // Create a new task group (18 concurrent request maximum, and up to 1024 outstanding submissions)
    anyblob::network::TaskedSendReceiverGroup group(18, 1024);

    // Create an AnyBlob scheduler object for the group
    anyblob::network::TaskedSendReceiver sendReceiver(group);

    // Create the provider for the corresponding filename
    auto provider = anyblob::cloud::Provider::makeProvider(bucketName, "", "", &sendReceiver);

    // Create the request
    vector<unique_ptr<anyblob::network::Transaction>> requests;
    requests.emplace_back(make_unique<anyblob::network::GetTransaction>(fileName));

    // Fetch the request synchronously with the scheduler object on this thread
    provider->retrieveObjects(sendReceiver, requests);

    // Get and print the result, note that the data lies in the data buffer but after the offset to skip the HTTP header
    // Note that the size is already without the header, so the full request has size + offset length
    auto& request = requests.front();
    string_view dataString(reinterpret_cast<char*>(request->data.get()) + request->offset, request->size);
    cout << dataString << endl;

    return 0;
}
//---------------------------------------------------------------------------
