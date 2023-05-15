# AnyBlobBenchmark
This directory contains the AnyBlob benchmarking tools, and also includes all third-parties (e.g., AWS SDK) to run our experiments of the AnyBlob paper.

# Validate Experiments

## AnyBlob Experiments

To validate our findings you need to set-up an AWS storage account to create randomly generated data objects.
Further, set up an instance with IAM Storage permissions and copy the files to this node.
For throughput experiments, we recommend the c5n.18xlarge, but a cheaper instance such as the c5n.xlarge can be used for latency experiments.

With the create.sh script from the scripts subdirectory, you can create random objects with different sizes.
All .sh files within the script folder need some small adaptations to include your random object bucket and also the result bucket to write back the experimental results.

The raw data of our Figures were created from multiple scripts. This list contains the mapping between individual scripts and Section / Figures.

- sizes_latency.sh for Figure 2 (Section 2.3)
- latency.sh for Figure 3 (Section 2.3)
- latency_sparse.sh for Figure 4 (Section 2.3)
- throughput_sparse.sh for Figures 5, 6 & 7 (Section 2.4)
- sizes.sh for Figure 8 (Section 2.5)
- https.sh for Figure 9 (Section 2.6)
- model.sh for Figure 10 (Section 2.8) and Figure 12 (Section 3.4)

In the following, we show the steps to compile our benchmarking binary.

	sudo apt update && sudo apt install liburing-dev openssl libssl-dev libjemalloc-dev g++ lld cmake libcurlpp-dev zlib1g-dev libcurl4-openssl-dev
	git clone --recursive https://github.com/durner/AnyBlob
	cd AnyBlob/example/benchmark
	mkdir -p build/Release
	cd build/Release
	cmake ../.. -DCMAKE_BUILD_TYPE=Release
	make -j16

## Database Experiments

All our DBMS related experiments and our binary of our proprietary system are shared in https://gitlab.db.in.tum.de/durner/cloud-storage-analytics.
