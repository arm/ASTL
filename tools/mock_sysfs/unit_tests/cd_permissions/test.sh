#!/bin/env bash
set -eu -o pipefail

find . -type d | sort | tee "${TEST_SCRATCH}/directory_list.txt"

for CUR_DIR in $(cat "${TEST_SCRATCH}/directory_list.txt")
do
    echo "--- ${CUR_DIR} ---"
    ls "${CUR_DIR}" | sort
done
