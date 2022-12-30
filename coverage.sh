#!/bin/bash

# Runs the coverage
mkdir -p build/Coverage
cd build/Coverage
cmake ../.. -DCMAKE_BUILD_TYPE=Coverage
make
./tester
make coverage
