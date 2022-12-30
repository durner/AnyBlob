#!/bin/bash
#------------------------------------------------------
# Export Settings
#------------------------------------------------------
SCRIPT_DIR="$(dirname $(readlink -f $0))"
export CONTAINER=anyblob-data-sparse
export CONTAINER_RESULTS=anyblob-results
export ACCOUNT=anyblob@anyblob.iam.gserviceaccount.com
export PRIVATEKEY=~/.ssh/anyblob_key.pem
if [[ -z "${REGION}" ]]; then
    export REGION=europe-west1
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
gsutil cp -r ${SCRIPT_DIR}/results/ gs://${CONTAINER_RESULTS}/latency_sparse/${timestamp}/
