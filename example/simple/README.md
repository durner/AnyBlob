# SimpleAnyBlob
This directory contains a simple AnyBlob retrieval executable for objects.

## Build

	sudo apt update && sudo apt install liburing-dev openssl libssl-dev libjemalloc-dev g++ lld cmake
	git clone --recursive https://github.com/durner/AnyBlob
	cd AnyBlob/example/simple
	mkdir -p build/Release
	cd build/Release
	cmake ../.. -DCMAKE_BUILD_TYPE=Release
	make -j16

