import csv
import os
from datetime import datetime
import multiprocessing

runs = range(0, 2)
bucket = os.environ['CONTAINER']
region = os.environ['REGION']

files = ["524288/", "1048576/", "2097152/", "4194304/", "8388608/", "16777216/", "33554432/"]
numberOfFiles = 4096
requestsScale = 256
rquestsMax = 50000
systems = ["uring", "s3crt"]
threadsUring = [1, 12]
threadsS3 = []
throughputGoal = [100]
concurrentRequestsS3Crt = [1, 512]
concurrentRequestsS3 = []
concurrentRequestsUring = [1, 18]
dnsresolver = ["throughput", "aws"]
iterations = 3

for r in runs:
    for system in systems:
        threadOrThroughput = []
        conccurency = []
        if system == "uring":
            threadOrThroughput = threadsUring
            conccurency = concurrentRequestsUring
        elif system == "s3crt":
            threadOrThroughput = throughputGoal
            conccurency = concurrentRequestsS3Crt
        elif system == "s3":
            threadOrThroughput = threadsS3
            conccurency = concurrentRequestsS3

        for f in files:
            for c in conccurency:
                for t in threadOrThroughput:
                    for d in dnsresolver:
                        if system != "uring" and d != "throughput":
                            continue
                        if system != "uring" and c < t:
                            continue

                        dirpath = os.path.dirname(os.path.realpath(__file__))
                        binPath = "timeout 600 " + dirpath + '/../build/Release/'
                        binPath = binPath + "AnyBlobBenchmark aws bandwidth -i " + str(iterations)
                        requests = requestsScale
                        if system == "uring":
                            requests = min(requestsScale * c * t, rquestsMax)
                        else:
                            requests = min(requestsScale * c, rquestsMax)

                        binPath = binPath + " -b " + bucket + " -r " + region + " -l " + str(requests) + " -f " + f + " -n " + str(numberOfFiles)
                        binPath = binPath + " -c " + str(c) + " -t " + str(t) + " -a " + system + " -d " + d + " -o sizes_" + str(r) + ".csv"

                        print(binPath)
                        os.system(binPath)
