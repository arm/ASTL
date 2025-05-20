#!/bin/bash

# Overview
#   When coverage is generated in parallel, each .gcda (coverage database) must be written as a separate file per process to avoid multiple-writer issues.
#   Unfortunately, gcovr does not handle the alternate .gcda names very well.
#   Therefore, we must merge the coverage from parallel runs and rename the merged files using names that gcovr understands.
#
# Warning
#   This script modifies the .gcda coverage files in place, and deletes the original ones so gcovr doesn't try to consume them.
#   Partial runs of this script will corrupt the coverage.

set -eu -o pipefail

if [ $# -ne 1 ]
then
    echo "❌ Error.  Missing build dir argument.  Usage: $0 <build_dir>" > /dev/stderr
    exit 1
fi

BUILD_DIR=$(realpath $1)

if [ ! -d "${BUILD_DIR}" ]
then
    echo "❌ Error.  ${BUILD_DIR} does not exist."
    exit 1
fi

if parallel --version > /dev/null 2>&1
then
    echo "✅ parallel is installed"
else
    echo "❌ Error.  parallel is not installed, or you have some non-gnu parallel that doesn't work the same."
    echo "👉 Please install GNU parallel."
    exit 1
fi

TMP_DIR=$(mktemp -d /tmp/merge_coverage_XXXX)
#TMP_DIR=/tmp/merge_coverage
#if [ -d "${TMP_DIR}" ]
#then
#    rm -r /tmp/merge_coverage
#fi
#mkdir -p "${TMP_DIR}"

# ${TMP_DIR}/index_all_coverage_dirs.txt will contain paths like this:
# ~/ASTL/build/debug/samples/sample_test/coverage_29042
#
# ${TMP_DIR}/index_test_dirs.txt will contain paths that look like this:
# ~/ASTL/build/debug/samples/sample_test
NUM_TEST_DIRS=$(find $BUILD_DIR -type f | grep "\.gcda" | parallel 'dirname' {} | sort -u | tee "${TMP_DIR}/index_all_coverage_dirs.txt" | parallel 'dirname' {} | sort -u | tee "${TMP_DIR}/index_test_dirs.txt" | wc -l)
echo "Found ${NUM_TEST_DIRS} test directories containing coverage"
echo "Outputted index to ${TMP_DIR}/index_test_dirs.txt"

# Pass 1 - Merge the parallel results from each test suite into one directory per test suite
mkdir "${TMP_DIR}/finished_merges/"
for CUR_TEST_DIR in $(cat "${TMP_DIR}/index_test_dirs.txt")
do

    echo "Merging all coverage in test directory ${CUR_TEST_DIR}"
    NUM_DIRS_TO_MERGE=$(parallel grep ::: "${CUR_TEST_DIR}" ::: "${TMP_DIR}/index_all_coverage_dirs.txt" | tee "${TMP_DIR}/index_current_coverage_dirs.txt" | wc -l)
    echo "Found ${NUM_DIRS_TO_MERGE} coverage directories to merge for this 1 test directory"

    FIRST_DIR_TO_MERGE=$(head -1 "${TMP_DIR}/index_current_coverage_dirs.txt")
    mv "${FIRST_DIR_TO_MERGE}" "${TMP_DIR}/accumulator_in"
    echo "Merging first file: ${FIRST_DIR_TO_MERGE}"

    # This works even when there is only a single file.  No merging will occur, it will be passed through
    for CURRENT_DIR_TO_MERGE in $(tail --lines=+2 "${TMP_DIR}/index_current_coverage_dirs.txt")
    do
        echo "Merging next file: ${CURRENT_DIR_TO_MERGE}"
        gcov-tool merge -o "${TMP_DIR}/accumulator_out" "${TMP_DIR}/accumulator_in" "${CURRENT_DIR_TO_MERGE}"
        rm -rf "${TMP_DIR}/accumulator_in" "${CURRENT_DIR_TO_MERGE}"
        mv "${TMP_DIR}/accumulator_out" "${TMP_DIR}/accumulator_in"
    done

    FINISHED_MERGED_DIR=$(mktemp -d "${TMP_DIR}/finished_merges/XXXX")
    mv "${TMP_DIR}/accumulator_in" "$FINISHED_MERGED_DIR/"
    echo "Saved merged results to ${FINISHED_MERGED_DIR}"
done

# Pass 2 - Merge the coverage data from multiple tests suites into one output
NUM_TESTSUITE_DIRS=$(ls ${TMP_DIR}/finished_merges | tee "${TMP_DIR}/index_all_test_suite_dirs.txt" | wc -l)
echo "Created ${NUM_TESTSUITE_DIRS} test suite directories, combining coverage data from multiple parallel threads"
FIRST_TEST_SUITE_DIR_TO_MERGE=$(head -1 "${TMP_DIR}/index_all_test_suite_dirs.txt")
mv "${TMP_DIR}/finished_merges/${FIRST_TEST_SUITE_DIR_TO_MERGE}" "${TMP_DIR}/accumulator_in"
echo "Merging first directory: ${FIRST_DIR_TO_MERGE}"
for SUBDIR in $(tail --lines=+2 "${TMP_DIR}/index_all_test_suite_dirs.txt")
do
    CURRENT_DIR_TO_MERGE="${TMP_DIR}/finished_merges/${SUBDIR}"
    echo "Merging next directory: ${CURRENT_DIR_TO_MERGE}"
    gcov-tool merge -o "${TMP_DIR}/accumulator_out" "${TMP_DIR}/accumulator_in" "${CURRENT_DIR_TO_MERGE}"
    rm -rf "${TMP_DIR}/accumulator_in" "${CURRENT_DIR_TO_MERGE}"
    mv "${TMP_DIR}/accumulator_out" "${TMP_DIR}/accumulator_in"
done
MERGED_SUITES_DIR=${TMP_DIR}/merged_suites
mv "${TMP_DIR}/accumulator_in" "${MERGED_SUITES_DIR}"
echo "Saved merged results to ${MERGED_SUITES_DIR}"

# Pass 3 - Demangle file names in /tmp/merge_coverage_XXXX/merged_suites/accumulator_in
for CUR_MANGLED_INPUT_FILE in $(find "${MERGED_SUITES_DIR}" -type f)
do
    DEMANGED_FILE_NAME=$(echo "${CUR_MANGLED_INPUT_FILE}" | sed 'sX.*accumulator_in/XX' | sed 'sX#X/Xg')
    echo "Demangled file: ${CUR_MANGLED_INPUT_FILE} -> ${DEMANGED_FILE_NAME}"
    # if the demangled file already exists, gcov-tool merge the two
    if [ -f "${DEMANGED_FILE_NAME}" ]
    then
        echo "❌ Error.  ${DEMANGED_FILE_NAME} already exists."
        exit 1
    else
        mv "${CUR_MANGLED_INPUT_FILE}" "${DEMANGED_FILE_NAME}"
    fi
done

echo "Cleaning up temporary scratch space: ${TMP_DIR}"
rm -rf "${TMP_DIR}"
