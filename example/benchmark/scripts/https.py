import os

runs = range(0, 2)
bucket = os.environ['CONTAINER']
region = os.environ['REGION']

files = ["16777216/"]
numberOfFiles = 4096
requestsScale = 256
rquestsMax = 50000
systems = ["s3", "uring"]
threadsUring = [12, 16, 20, 24]
threadsS3 = [64, 128, 192, 256]
throughputGoal = []
concurrentRequestsS3Crt = []
concurrentRequestsS3 = [256, 512]
concurrentRequestsUring = [6, 8, 14, 16, 18, 20]
https = [0, 1]
encryption = [0, 1]
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
                    for h in https:
                        for e in encryption:
                            dirpath = os.path.dirname(os.path.realpath(__file__))
                            binPath = "timeout 600 " + dirpath + '/../build/Release/'
                            binPath = binPath + "AnyBlobBenchmark aws bandwidth -i " + str(iterations)
                            requests = requestsScale
                            if system == "uring":
                                requests = min(requestsScale * c * t, rquestsMax)
                            else:
                                requests = min(requestsScale * c, rquestsMax)

                            binPath = binPath + " -b " + bucket + " -r " + region + " -l " + str(requests) + " -n " + str(numberOfFiles)
                            if e == 1:
                                if system != "uring":
                                    continue
                                else:
                                    binPath = binPath + " -f enc/" + f
                            else:
                                binPath = binPath + " -f " + f
                            binPath = binPath + " -c " + str(c) + " -t " + str(t) + " -a " + system + " -h " + str(h) + " -x " + str(e) + " -o sizes_" + str(r) + ".csv"

                            print(binPath)
                            os.system(binPath)
