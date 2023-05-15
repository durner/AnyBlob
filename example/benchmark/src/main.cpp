#include "benchmark/bandwidth.hpp"
#include "cloud/aws.hpp"
#include "cloud/azure.hpp"
#include "cloud/gcp.hpp"
#include "cloud/ibm.hpp"
#include "cloud/oracle.hpp"
#include <cstring>
#include <iostream>
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
    helpText += "cloudProvider: [aws, gcp, oracle, ibm, azure]\n";
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
    helpText += "-a algorithm [uring, s3, s3crt]\n";
    helpText += "-o csv output file\n";
    helpText += "-d dnsresolver (default: throughput [aws])\n";
    helpText += "-e clientEmail [required for IBM, Oracle, GCP & Azure]\n";
    helpText += "-k rsaKeyFile [required for IBM, Oracle, GCP & Azure]\n";
    helpText += "-u upload test (default: 0)\n";
    helpText += "-h https (default: 0)\n";
    helpText += "-x encryption at rest (default: 0)\n";

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

    string s;
    for (auto i = 3; i < argc; i++) {
        if ((i + 1) >= argc)
            return -1;
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
            else
                bandwithSettings.systems = {anyblob::benchmark::Bandwidth::Systems::Uring};
        } else if (!strcmp(argv[i], "-l")) {
            bandwithSettings.requests = atoi(argv[++i]);
        } else {
            cerr << helpText << endl;
            return -1;
        }
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

    if (!strcmp(argv[1], "gcp") && bandwithSettings.systems.front() == anyblob::benchmark::Bandwidth::Systems::Uring && !bandwithSettings.account.empty() && !bandwithSettings.rsaKeyFile.empty()) {
        string uri = "gcp://" + gcpSettings.bucket + ":" + gcpSettings.region + "/";
        anyblob::benchmark::Bandwidth::run(bandwithSettings, uri);
    }

    if (!strcmp(argv[1], "azure") && bandwithSettings.systems.front() == anyblob::benchmark::Bandwidth::Systems::Uring && !bandwithSettings.account.empty() && !bandwithSettings.rsaKeyFile.empty()) {
        string uri = "azure://" + azureSettings.container + "/";
        anyblob::benchmark::Bandwidth::run(bandwithSettings, uri);
    }

    return 0;
}
//---------------------------------------------------------------------------
