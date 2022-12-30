#!/bin/bash
#------------------------------------------------------
# Export Settings
#------------------------------------------------------
SCRIPT_DIR="$(dirname $(readlink -f $0))"
SIZES=( $(( 2**24 )) )
CONTAINER=anyblob-data
export ACCOUNT=anyblob
export PRIVATEKEY=~/.ssh/anyblob_key
key="$(cat $PRIVATEKEY)"
#------------------------------------------------------
for size in ${SIZES[@]}
do
    for n in {1..4096}
    do
        openssl rand -out ${n}.bin $size
        az storage blob upload -f $n.bin -c ${CONTAINER} -n ${size}/${n}.bin --account-name $ACCOUNT --account-key $key
        rm ${n}.bin
    done
done
#------------------------------------------------------
