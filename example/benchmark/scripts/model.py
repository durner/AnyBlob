import csv
import os
from datetime import datetime
import multiprocessing

runs = range(0, 1)
bucket = os.environ['CONTAINER']
region = os.environ['REGION']

files = "16777216/"
numberOfFiles = 4096
requestsScale = 256
rquestsMax = 50000
systems = ["uring", "s3crt", "s3"]
threadsUring = [1, 4, 8, 10, 12, 14]
threadsS3 = [1, 8, 32, 64, 128, 192, 256]
throughputGoal = [1, 10, 25, 50, 100]
concurrentRequestsS3Crt = [1, 128, 256, 512]
concurrentRequestsS3 = [1, 256, 512]
concurrentRequestsUring = [1, 16, 18, 20]
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

                    binPath = binPath + " -b " + bucket + " -r " + region + " -l " + str(requests) + " -f " + files + " -n " + str(numberOfFiles)
                    binPath = binPath + " -c " + str(c) + " -t " + str(t) + " -a " + system + " -d " + d + " -o model_" + str(r) + ".csv"

                    print(binPath)
                    os.system(binPath)
