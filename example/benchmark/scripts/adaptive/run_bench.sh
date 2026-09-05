#!/bin/bash
# usage: BUCKET=... REGION=... BLOBPATH=... BLOBFILES=... run_bench.sh <name> <algo> <https> <threads> <concurrency> <requests> [extra benchmark args...]

set -e
SCRIPT_DIR="$(dirname $(readlink -f $0))"
BIN=${SCRIPT_DIR}/../../build/Release/AnyBlobBenchmark
RESULTS=${SCRIPT_DIR}/results
mkdir -p ${RESULTS}

NAME=$1
ALGO=$2
TLS=$3
T=$4
C=$5
L=$6
shift 6

# Increase the default 1024 fd soft limit
ulimit -n $(ulimit -Hn)

cpuBefore=$(awk '/^cpu /{print $2+$3+$4+$7+$8}' /proc/stat)
start=$(date +%s.%N)
timeout 900 ${BIN} aws bandwidth -b ${BUCKET} -r ${REGION} -f ${BLOBPATH} -n ${BLOBFILES} \
    -a ${ALGO} -h ${TLS} -t ${T} -c ${C} -l ${L} -o ${RESULTS}/${NAME}.csv "$@" 2>&1 | tee ${RESULTS}/${NAME}.log
end=$(date +%s.%N)
cpuAfter=$(awk '/^cpu /{print $2+$3+$4+$7+$8}' /proc/stat)

# name, algo, tls, threads, concurrency, requests, wall seconds, busy jiffies (all cores, USER_HZ=100)
echo "${NAME},${ALGO},${TLS},${T},${C},${L},$(awk -v a=${start} -v b=${end} 'BEGIN{print b-a}'),$((cpuAfter - cpuBefore))" >> ${RESULTS}/cpu.csv
