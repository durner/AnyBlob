#include "benchmark/bandwidth.hpp"
#include "cloud/aws.hpp"
#include "cloud/azure.hpp"
#include "cloud/gcp.hpp"
#include "cloud/ibm.hpp"
#include "cloud/oracle.hpp"
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <aws/core/Aws.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    string helpText = "AnyBlobBenchmark cloudProvider benchmark [OPTIONS]\n\n";
    helpText += "cloudProvider: [aws, gcp, oracle, ibm, azure, onelake]\n";
    helpText += "benchmark: [bandwidth]\n\n";
    helpText += "OPTIONS:\n";
    helpText += "-b bucketName\n";
    helpText += "-r regionName\n";
    helpText += "-f blobPath (if dictionary we use number.bin)\n";
    helpText += "-n maxBlobFileNumber [optional, if -f is path]\n";
    helpText += "-l maximum Number of Requests\n";
    helpText += "-c concurrent Outstanding Requests per Socket/Thread\n";
    helpText += "-t concurrent Number of Threads\n";
    helpText += "-i number of iterations (default: 1)\n";
    helpText += "-s chunkSize\n";
    helpText += "-a algorithm [uring, s3, s3crt, azuresdk]\n";
    helpText += "-o csv output file\n";
    helpText += "-d dnsresolver (default: throughput [aws])\n";
    helpText += "-e clientEmail [required for IBM, Oracle, GCP & Azure]\n";
    helpText += "-k rsaKeyFile [required for IBM, Oracle, GCP & Azure]\n";
    helpText += "-u upload test (default: 0)\n";
    helpText += "-h https (default: 0)\n";
    helpText += "-x encryption at rest (default: 0)\n";
    helpText += "OneLake: -b workspace --object-path lakehouse.Lakehouse/Files/path\n";
    helpText += "--read-offset bytes (default: 0), --read-bytes bytes (default: 1048576)\n";
    helpText += "--request-timeout-ms milliseconds (OneLake default: 5000)\n";
    helpText += "OneLake uses HTTPS and AZURE_STORAGE_BEARER_TOKEN from the environment.\n";

    if (argc < 3 || strcmp(argv[2], "bandwidth")) {
        cerr << helpText << endl;
        return -1;
    }

    anyblob::cloud::AWS::Settings awsSettings;
    anyblob::cloud::GCP::Settings gcpSettings;
    anyblob::cloud::IBM::Settings ibmSettings;
    anyblob::cloud::Oracle::Settings oracleSettings;
    anyblob::cloud::Azure::Settings azureSettings;

    anyblob::benchmark::Bandwidth::Settings bandwithSettings;
    bandwithSettings.systems = {anyblob::benchmark::Bandwidth::Systems::Uring};
    bandwithSettings.oneLake = !strcmp(argv[1], "onelake");
    if (bandwithSettings.oneLake)
        bandwithSettings.https = true;

    string s;
    for (auto i = 3; i < argc; i++) {
        if ((i + 1) >= argc)
            return -1;
        if (bandwithSettings.oneLake && (string_view("-n") == argv[i] || string_view("-c") == argv[i] || string_view("-s") == argv[i] || string_view("-t") == argv[i] || string_view("-i") == argv[i] || string_view("-u") == argv[i] || string_view("-h") == argv[i] || string_view("-x") == argv[i] || string_view("-l") == argv[i])) {
            const string value = argv[i + 1];
            try {
                if (value.empty() || value.find_first_not_of("0123456789") != string::npos || stoull(value) > numeric_limits<int>::max())
                    throw invalid_argument("Invalid integer");
            } catch (const exception&) {
                cerr << "OneLake numeric options require a non-negative integer in range" << endl;
                return 2;
            }
        }
        if (!strcmp(argv[i], "-b")) {
            awsSettings.bucket = argv[++i];
            gcpSettings.bucket = awsSettings.bucket;
            ibmSettings.bucket = awsSettings.bucket;
            oracleSettings.bucket = awsSettings.bucket;
            azureSettings.container = awsSettings.bucket;
        } else if (!strcmp(argv[i], "-r")) {
            awsSettings.region = argv[++i];
            gcpSettings.region = awsSettings.region;
            oracleSettings.region = awsSettings.region;
            ibmSettings.region = awsSettings.region;
        } else if (!strcmp(argv[i], "-f")) {
            bandwithSettings.filePath = argv[++i];
        } else if (!strcmp(argv[i], "--object-path")) {
            bandwithSettings.objectPath = argv[++i];
        } else if (!strcmp(argv[i], "--read-offset") || !strcmp(argv[i], "--read-bytes") || !strcmp(argv[i], "--request-timeout-ms")) {
            const bool offset = !strcmp(argv[i], "--read-offset");
            const bool timeout = !strcmp(argv[i], "--request-timeout-ms");
            const string value = argv[++i];
            if (value.empty() || value.find_first_not_of("0123456789") != string::npos) {
                cerr << "Expected a non-negative byte count" << endl;
                return 2;
            }
            try {
                auto bytes = stoull(value);
                if (offset)
                    bandwithSettings.readOffset = bytes;
                else if (timeout)
                    bandwithSettings.requestTimeoutMs = bytes;
                else
                    bandwithSettings.readLength = bytes;
            } catch (const exception&) {
                cerr << "Byte count is out of range" << endl;
                return 2;
            }
        } else if (!strcmp(argv[i], "-o")) {
            bandwithSettings.report = argv[++i];
        } else if (!strcmp(argv[i], "-d")) {
            bandwithSettings.resolver = argv[++i];
        } else if (!strcmp(argv[i], "-e")) {
            bandwithSettings.account = argv[++i];
        } else if (!strcmp(argv[i], "-k")) {
            bandwithSettings.rsaKeyFile = argv[++i];
        } else if (!strcmp(argv[i], "-n")) {
            bandwithSettings.blobFiles = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-c")) {
            bandwithSettings.concurrentRequests = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-s")) {
            bandwithSettings.chunkSize = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-t")) {
            bandwithSettings.concurrentThreads = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-i")) {
            bandwithSettings.iterations = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-u")) {
            bandwithSettings.testUpload = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-h")) {
            bandwithSettings.https = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-x")) {
            bandwithSettings.encryption = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-a")) {
            ++i;
            if (!strcmp(argv[i], "s3"))
                bandwithSettings.systems = {anyblob::benchmark::Bandwidth::Systems::S3};
            else if (!strcmp(argv[i], "s3crt"))
                bandwithSettings.systems = {anyblob::benchmark::Bandwidth::Systems::S3Crt};
            else if (!strcmp(argv[i], "azuresdk"))
                bandwithSettings.systems = {anyblob::benchmark::Bandwidth::Systems::AzureSdk};
            else if (strcmp(argv[i], "uring")) {
                cerr << "Unknown algorithm; expected uring, s3, s3crt, or azuresdk" << endl;
                return 2;
            } else {
                bandwithSettings.systems = {anyblob::benchmark::Bandwidth::Systems::Uring};
            }
        } else if (!strcmp(argv[i], "-l")) {
            bandwithSettings.requests = atoi(argv[++i]);
        } else {
            cerr << helpText << endl;
            return -1;
        }
    }

    if (!bandwithSettings.oneLake
        && bandwithSettings.systems.front() == anyblob::benchmark::Bandwidth::Systems::AzureSdk) {
        cerr << "The azuresdk algorithm is only supported by the onelake provider" << endl;
        return 2;
    }

    if (bandwithSettings.oneLake) {
        const auto system = bandwithSettings.systems.front();
        constexpr uint64_t maxAzureWorkers = 1024;
        constexpr uint64_t maxAzureBufferBytes = 1ull << 30;
        const auto configuredInFlight
            = static_cast<uint64_t>(bandwithSettings.concurrentThreads) * bandwithSettings.concurrentRequests;
        if ((system != anyblob::benchmark::Bandwidth::Systems::Uring && system != anyblob::benchmark::Bandwidth::Systems::AzureSdk)
            || !bandwithSettings.https || bandwithSettings.testUpload || bandwithSettings.encryption) {
            cerr << "OneLake requires -a uring or azuresdk, -h 1 -u 0 -x 0 (no uploads or client-side decryption)" << endl;
            return 2;
        }
        if (azureSettings.container.empty() || azureSettings.container.find_first_of("/\r\n ?#") != string::npos ||
            bandwithSettings.objectPath.empty() || bandwithSettings.objectPath.front() == '/' || bandwithSettings.objectPath.find_first_of("\r\n ?#") != string::npos ||
            !bandwithSettings.concurrentThreads || !bandwithSettings.concurrentRequests || !bandwithSettings.requests || !bandwithSettings.iterations || !bandwithSettings.chunkSize ||
            !bandwithSettings.requestTimeoutMs || bandwithSettings.requestTimeoutMs > 60000 ||
            bandwithSettings.readLength < 2 || bandwithSettings.readOffset > numeric_limits<uint64_t>::max() - bandwithSettings.readLength ||
            (system == anyblob::benchmark::Bandwidth::Systems::AzureSdk
                && (bandwithSettings.readOffset > static_cast<uint64_t>(numeric_limits<int64_t>::max())
                    || bandwithSettings.readLength > static_cast<uint64_t>(numeric_limits<int64_t>::max())
                    || bandwithSettings.readLength > numeric_limits<size_t>::max()
                    || bandwithSettings.readOffset + bandwithSettings.readLength - 1 > static_cast<uint64_t>(numeric_limits<int64_t>::max())
                    || configuredInFlight > maxAzureWorkers
                    || configuredInFlight > maxAzureBufferBytes / bandwithSettings.readLength)) ||
            bandwithSettings.requests > numeric_limits<uint64_t>::max() / bandwithSettings.readLength) {
            cerr << "OneLake requires a workspace, URL-encoded relative object path, positive run settings, and a valid byte range of at least 2 bytes" << endl;
            return 2;
        }
        try {
            anyblob::benchmark::Bandwidth::run(bandwithSettings, "azure://" + azureSettings.container + "/");
        } catch (const exception& error) {
            cerr << error.what() << endl;
            return 1;
        }
        return 0;
    }

    if (!strcmp(argv[1], "aws")) {
        Aws::SDKOptions options;
        Aws::InitAPI(options);

        string uri = "s3://" + awsSettings.bucket + ":" + awsSettings.region + "/";
        anyblob::benchmark::Bandwidth::run(bandwithSettings, uri);

        Aws::ShutdownAPI(options);
    }

    if (!strcmp(argv[1], "oracle") && bandwithSettings.systems.front() == anyblob::benchmark::Bandwidth::Systems::Uring && !bandwithSettings.account.empty() && !bandwithSettings.rsaKeyFile.empty()) {
        string uri = "oci://" + oracleSettings.bucket + ":" + oracleSettings.region + "/";
        anyblob::benchmark::Bandwidth::run(bandwithSettings, uri);
    }

    if (!strcmp(argv[1], "ibm") && bandwithSettings.systems.front() == anyblob::benchmark::Bandwidth::Systems::Uring && !bandwithSettings.account.empty() && !bandwithSettings.rsaKeyFile.empty()) {
        string uri = "ibm://" + ibmSettings.bucket + ":" + ibmSettings.region + "/";
        anyblob::benchmark::Bandwidth::run(bandwithSettings, uri);
    }

    if (!strcmp(argv[1], "gs") && bandwithSettings.systems.front() == anyblob::benchmark::Bandwidth::Systems::Uring && !bandwithSettings.account.empty() && !bandwithSettings.rsaKeyFile.empty()) {
        string uri = "gs://" + gcpSettings.bucket + ":" + gcpSettings.region + "/";
        anyblob::benchmark::Bandwidth::run(bandwithSettings, uri);
    }

    if (!strcmp(argv[1], "azure") && bandwithSettings.systems.front() == anyblob::benchmark::Bandwidth::Systems::Uring && !bandwithSettings.account.empty() && !bandwithSettings.rsaKeyFile.empty()) {
        string uri = "azure://" + azureSettings.container + "/";
        anyblob::benchmark::Bandwidth::run(bandwithSettings, uri);
    }

    return 0;
}
//---------------------------------------------------------------------------
