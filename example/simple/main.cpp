#include "cloud/provider.hpp"
#include "network/transaction.hpp"
#include "network/tasked_send_receiver.hpp"
#include <cstring>
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
    auto bucketName = "s3://anyblob:eu-central-1";
    auto fileName = "anyblob/anyblob.txt";

    // Create a new task group
    anyblob::network::TaskedSendReceiverGroup group;

    // Create an AnyBlob scheduler object for the group
    anyblob::network::TaskedSendReceiver sendReceiver(group);

    // Create the provider for the corresponding filename
    auto provider = anyblob::cloud::Provider::makeProvider(bucketName, false, "", "", &sendReceiver);

    // Update the concurrency according to instance settings
    auto config = provider->getConfig(sendReceiver);
    group.setConfig(config);

    // Create the get request
    anyblob::network::Transaction getTxn(provider.get());
    getTxn.getObjectRequest(fileName);

    // Retrieve the request synchronously with the scheduler object on this thread
    getTxn.processSync(sendReceiver);

    // Get and print the result
    for (const auto& it : getTxn) {
        // Check if the request was successful
        if (!it.success()) {
            cout << "Request was not successful!" << endl;
            continue;
        }
        // Simple string_view interface
        cout << it.getResult() << endl;

        // Advanced raw interface
        // Note that the data lies in the data buffer but after the offset to skip the HTTP header
        // Note that the size is already without the header, so the full request has size + offset length
        string_view rawDataString(reinterpret_cast<const char*>(it.getData()) + it.getOffset(), it.getSize());
        cout << rawDataString << endl;
    }

    return 0;
}
//---------------------------------------------------------------------------
