#!/usr/bin/env bash
set -eu -o pipefail

START_DIR=$(pwd)
echo '--- Directory List ---'
find . -type d | sort | tee "${TEST_SCRATCH}/directory_list.txt"

for CUR_DIR in $(cat "${TEST_SCRATCH}/directory_list.txt"); do
	cd "${START_DIR}"
	echo "--- ${CUR_DIR} ---"
	cd "${CUR_DIR}"
	ls | sort
done
