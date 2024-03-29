#!/bin/bash
#------------------------------------------------------
# Export Settings
#------------------------------------------------------
SCRIPT_DIR="$(dirname $(readlink -f $0))"
SIZES=($(( 2**0 )) $(( 2**10 )) $(( 2**19 )) $(( 2**20 )) $(( 2**21 )) $(( 2**22 )) $(( 2**23 )) $(( 2**24 )) $(( 2**25 )))
CONTAINER=anyblob-data
if [[ -z "${REGION}" ]]; then
    export REGION=eu-central-1
else
    CONTAINER=anyblob-data-${REGION}
fi
#------------------------------------------------------
aws s3api create-bucket --bucket $CONTAINER --region $REGION --create-bucket-configuration LocationConstraint=$REGION
#------------------------------------------------------
mkdir data
mkdir data/enc
cd data
for size in ${SIZES[@]}
do
    for n in {1..8192}
    do
        openssl rand -out ${n}.bin $size
        openssl enc -aes-256-cbc -K "3031323334353637383930313233343536373839303132333435363738393031" -iv "30313233343536373839303132333435" -in ${n}.bin -out enc/${n}.bin
    done
    aws s3 sync ./ s3://${CONTAINER}/${size}/
    for n in {1..8192}
    do
        rm ${n}.bin
        rm enc/${n}.bin
    done
done
#------------------------------------------------------
