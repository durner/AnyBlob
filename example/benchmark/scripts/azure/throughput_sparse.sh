#!/bin/bash
#------------------------------------------------------
# Export Settings
#------------------------------------------------------
SCRIPT_DIR="$(dirname $(readlink -f $0))"
export ACCOUNT=anyblob
export PRIVATEKEY=~/.ssh/anyblob_key
export REGION="'West Europe'"
export CONTAINER_RESULTS=anyblob-results
if [[ -z "${CONTAINER}" ]]; then
    export CONTAINER=anyblob-data-uring
fi
#------------------------------------------------------
rm -rf ${SCRIPT_DIR}/results
#------------------------------------------------------
python3 ${SCRIPT_DIR}/throughput_sparse.py
#------------------------------------------------------
mkdir ${SCRIPT_DIR}/results
mv *.csv ${SCRIPT_DIR}/results
mv *.csv.summary ${SCRIPT_DIR}/results
timestamp=$(date +%s)
key="$(cat $PRIVATEKEY)"
for f in ${SCRIPT_DIR}/results/*; do
    base="$(basename -- $f)"
    az storage blob upload -f $f -c ${CONTAINER_RESULTS} -n throughput_sparse/${timestamp}/$base --account-name $ACCOUNT --account-key $key
done
