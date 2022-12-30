#!/bin/bash
#------------------------------------------------------
# Export Settings
#------------------------------------------------------
SCRIPT_DIR="$(dirname $(readlink -f $0))"
SIZES=($(( 2**24 )))
CONTAINER=anyblob-data
#------------------------------------------------------
for size in ${SIZES[@]}
do
    for n in {0..4096}
    do
        openssl rand -out ${n}.bin $size
        gsutil cp ${n}.bin gs://${CONTAINER}/${size}/${n}.bin
        rm ${n}.bin
    done
done
#------------------------------------------------------
