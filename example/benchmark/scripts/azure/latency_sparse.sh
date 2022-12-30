#!/bin/bash
#------------------------------------------------------
# Export Settings
#------------------------------------------------------
SCRIPT_DIR="$(dirname $(readlink -f $0))"
export CONTAINER=anyblob-data-sparse
export CONTAINER_RESULTS=anyblob-results
export ACCOUNT=anyblob
export PRIVATEKEY=~/.ssh/anyblob_key
if [[ -z "${REGION}" ]]; then
    export REGION="'West Europe'"
fi
#------------------------------------------------------
rm -rf ${SCRIPT_DIR}/results
#------------------------------------------------------
python3 ${SCRIPT_DIR}/latency.py
#------------------------------------------------------
mkdir ${SCRIPT_DIR}/results
mv *.csv ${SCRIPT_DIR}/results
mv *.csv.summary ${SCRIPT_DIR}/results
timestamp=$(date +%s)
key="$(cat $PRIVATEKEY)"
for f in ${SCRIPT_DIR}/results/*; do
    base="$(basename -- $f)"
    az storage blob upload -f $f -c ${CONTAINER_RESULTS} -n latency_sparse/${timestamp}/$base --account-name $ACCOUNT --account-key $key
done
