#!/bin/bash
#------------------------------------------------------
# Export Settings
#------------------------------------------------------
SCRIPT_DIR="$(dirname $(readlink -f $0))"
export CONTAINER_RESULTS=anyblob-results
if [[ -z "${CONTAINER}" ]]; then
    export CONTAINER=anyblob-data
fi
if [[ -z "${REGION}" ]]; then
    export REGION=eu-central-1
fi
if [[ "${REGION}" != "eu-central-1" ]]; then
    export CONTAINER_RESULTS=anyblob-results-${REGION}
fi
#------------------------------------------------------
python3 ${SCRIPT_DIR}/throughput_sparse.py
#------------------------------------------------------
aws s3api create-bucket --bucket $CONTAINER_RESULTS --region $REGION --create-bucket-configuration LocationConstraint=$REGION
mkdir ${SCRIPT_DIR}/results
mv *.csv ${SCRIPT_DIR}/results
mv *.csv.summary ${SCRIPT_DIR}/results
timestamp=$(date +%s)
aws s3 cp ${SCRIPT_DIR}/results s3://${CONTAINER_RESULTS}/throughput_sparse/${timestamp}/ --recursive
