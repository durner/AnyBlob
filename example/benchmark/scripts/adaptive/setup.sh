#!/bin/bash
# One-time setup on the benchmark instance

set -e
sudo apt-get update
sudo apt-get install -y g++ cmake make lld libjemalloc-dev git liburing-dev libssl-dev pkg-config zlib1g-dev libcurl4-openssl-dev python3 sysstat unzip
if ! command -v aws >/dev/null; then
    curl -s "https://awscli.amazonaws.com/awscli-exe-linux-$(uname -m).zip" -o /tmp/awscliv2.zip
    (cd /tmp && unzip -q awscliv2.zip && sudo ./aws/install)
fi

SCRIPT_DIR="$(dirname $(readlink -f $0))"
mkdir -p ${SCRIPT_DIR}/../../build/Release
cd ${SCRIPT_DIR}/../../build/Release
cmake -DCMAKE_BUILD_TYPE=Release ../..
make -j$(nproc)
