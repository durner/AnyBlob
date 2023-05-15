import csv
import os
from datetime import datetime
import multiprocessing

runs = range(0, 2)
bucket = os.environ['CONTAINER']
region = os.environ['REGION']

files = "16777216/"
numberOfFiles = 4096
requestsScale = 256
rquestsMax = 50000
systems = []
threadsUring = [12]
threadsS3 = [128]
throughputGoal = [100]
concurrentRequestsS3Crt = [256]
concurrentRequestsS3 = [256]
concurrentRequestsUring = [20]
dnsresolver = ["throughput"]
iterations = 10

print(bucket)

if "s3crt" in bucket:
    systems = ["s3crt"]
elif "s3" in bucket:
    systems = ["s3"]
elif "throughput" in bucket:
    systems = ["uring"]
elif "uring" in bucket:
    systems = ["uring"]
    dnsresolver = ["aws"]

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
                    binPath = "timeout 6000 " + dirpath + '/../build/Release/'
                    binPath = binPath + "AnyBlobBenchmark aws bandwidth -i " + str(iterations)
                    requests = requestsScale
                    if system == "uring":
                        requests = min(requestsScale * c * t, rquestsMax)
                    else:
                        requests = min(requestsScale * c, rquestsMax)

                    binPath = binPath + " -b " + bucket + " -r " + region + " -l " + str(requests) + " -f " + files + " -n " + str(numberOfFiles)
                    binPath = binPath + " -c " + str(c) + " -t " + str(t) + " -a " + system + " -d " + d + " -o throughput_sparse_" + str(r) + ".csv"

                    print(binPath)
                    os.system(binPath)
