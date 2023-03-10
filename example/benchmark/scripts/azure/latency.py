import csv
import os
from datetime import datetime
import multiprocessing

runs = range(0, 2)
bucket = os.environ['CONTAINER']
region = os.environ['REGION']
account = os.environ['ACCOUNT']
key = os.environ['PRIVATEKEY']

files = "16777216/"
numberOfFiles = 4096
requestsScale = 256
rquestsMax = 50000
systems = ["uring"]
threadsUring = [1]
threadsS3 = [1]
throughputGoal = [1]
concurrentRequestsS3Crt = [1]
concurrentRequestsS3 = [1]
concurrentRequestsUring = [1]
dnsresolver = ["throughput"]
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


        for c in conccurency:
            for t in threadOrThroughput:
                for d in dnsresolver:
                    if system != "uring" and d != "throughput":
                        continue
                    if system != "uring" and c < t:
                        continue

                    dirpath = os.path.dirname(os.path.realpath(__file__))
                    binPath = "timeout 600 " + dirpath + '/../../build/Release/'
                    binPath = binPath + "AnyBlobBenchmark azure bandwidth -i " + str(iterations)
                    requests = requestsScale
                    if system == "uring":
                        requests = min(requestsScale * c * t, rquestsMax)
                    else:
                        requests = min(requestsScale * c, rquestsMax)

                    binPath = binPath + " -b " + bucket + " -r " + region + " -l " + str(requests) + " -f " + files + " -n " + str(numberOfFiles)
                    binPath = binPath + " -c " + str(c) + " -t " + str(t) + " -a " + system + " -d " + d + " -o benchmark_" + str(r) + ".csv"
                    binPath = binPath + " -e " + account + " -k " + key

                    print(binPath)
                    os.system(binPath)
