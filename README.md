# AnyBlob - Universal Cloud Object Storage Download Manager

In this repository, we present AnyBlob.
AnyBlob is a universal download manager that allows to retrieve and upload objects to different cloud object stores.
Our download manager uses less CPU resources than cloud-vendor provided libraries while retaining maximum throughput performance.
AnyBlob leverages IO\_uring for superior performance per core.
For experimental results, please visit our research paper at [].

## Building AnyBlob

AnyBlob relies on some third-party libraries:
- uring
- openssl
- jemalloc (recommended)

For Ubuntu the following command install the third-party libraries:
```
sudo apt update && sudo apt install liburing-dev openssl libssl-dev libjemalloc-dev lld g++ cmake
```

For building, use the following commands:
```
git clone --recursive https://github.com/durner/AnyBlob
mkdir -p build/Release
cd build/Release
cmake -DCMAKE_BUILD_TYPE=Release ../..
make -j16
```

## Using AnyBlob

AnyBlob is a modern C++ library that can be linked to your program.
We provide two examples how to use AnyBlob in your project.
Please find a simple example that does only download a single object from AWS, and a more sophisticated example that runs download benchmarks.
There, you can also find information on how to easily integrate the building process into existing CMake projects for an automated compilation and static linkage of AnyBlob.
The anyblob.cmake should be used in your project to integrate this repository as external project.

## Contribution

If you have bug fixes or improvement, please do not hesitate to open a new merge request.
For coverage testing you can simply `make coverage` and open the coverage report found in the build directory at coverage/index.html.

## Cite this work

If you are using AnyBlob in our scientific work, please cite:

```
Exploiting Cloud Object Storage for High-Performance Analytics
Dominik Durner, Viktor Leis, and Thomas Neumann
```
