#!/bin/env bash

# This script is a wrapper for Sysfs unit tests
# It launches a MockSysfs executable at a unique mount point
# then runs whatever command is passed as an argument

# TODO - https://jira.arm.com/browse/ASTL-74 - Add code coverage for MockSysfs

set -eu -o pipefail

TIMEOUT=30   # Maximum seconds to wait for guid
PATTERN_READY="eccf4f7c-d1b1-47f0-9d23-159f6d38b661"

if [ $# -lt 2 ]
then
    echo "❌ Error!  You must pass one or more commands to this script to run" > /dev/stderr
    echo "Usage: $0 <sysfs_executable> <expected_output> <command> [arg1] [arg2] ... [argN]" > /dev/stderr
    exit 1
fi

SYSFS_EXECUTABLE=$(realpath $1)
shift
EXPECTED_OUTPUT=$(realpath $1)
shift

if [ -f ${SYSFS_EXECUTABLE} ]
then
    echo "✅ Found MockSysfs binary at: ${SYSFS_EXECUTABLE}"
else
    echo "❌ Error!  Sysfs binary not found at: ${SYSFS_EXECUTABLE}" > /dev/stderr
    echo "Note: Did you forget to build it with CMake?" > /dev/stderr
    exit 1
fi

TMP_DIR=$(mktemp -d /tmp/sysfs_unit_test_XXXXXXXX)
if [ -d ${TMP_DIR} ]
then
    echo "✅ Created temporary directory at: ${TMP_DIR}"
else
    echo "❌ Error!  Failed to create empty mount point" > /dev/stderr
    exit 1
fi

SYSFS_LOG="${TMP_DIR}/sysfs_log.txt" # Combined stdout and stderr from MockSysfs process (and Valgrind)
VALGRIND_LOG="${TMP_DIR}/valgrind_log.txt"
MOUNT_POINT="${TMP_DIR}/mount" # Directory where sysfs will be mounted
ACTUAL_OUTPUT="${TMP_DIR}/actual.txt"
export TEST_SCRATCH="${TMP_DIR}/scratch"

mkdir ${MOUNT_POINT} ${TEST_SCRATCH}

wait_for() {
    local FILE="$1" DESC="$2" PATTERN="$3"
    echo "⏱️  Waiting (up to ${TIMEOUT}s) for '$PATTERN' in ${DESC}..."
    timeout "${TIMEOUT}" bash -c "
        stdbuf -oL tail -n +0 -F \"${FILE}\" | grep -m1 -F \"${PATTERN}\"
    "
    echo "✅ Detected '$PATTERN' in ${DESC}"
}

# If valgrind reports an "unhandled dwarf2 abbrev form code 0x25" error, try compiling with g++ instead of clang
valgrind --log-file=${VALGRIND_LOG} --leak-check=full --show-leak-kinds=all --track-origins=yes ${SYSFS_EXECUTABLE} -f -s ${MOUNT_POINT} &> ${SYSFS_LOG} &
# ${SYSFS_EXECUTABLE} -f -s ${MOUNT_POINT} &> ${SYSFS_LOG} &
SYSFS_PROCESS="$!"

if ps -p ${SYSFS_PROCESS} > /dev/null
then
    echo "✅ MockSysfs process launched with PID = ${SYSFS_PROCESS} and log ${SYSFS_LOG}"
else
    echo "❌ Error!  Failed to launch MockSysfs process" > /dev/stderr
    exit 1
fi

wait_for "${SYSFS_LOG}" "MockSysfs startup" "${PATTERN_READY}"

echo 'Start of test output...'

# Launch whatever command we want to use as our unit test
cd ${MOUNT_POINT}
$@ |& tee ${ACTUAL_OUTPUT}

if kill -SIGTERM ${SYSFS_PROCESS}
then
    echo "✅ Sent SIGTERM sysfs process to gracefully kill it"
else
    echo "❌ Error!  Failed to send kill signal to sysfs process with PID: ${SYSFS_PROCESS}" > /dev/stderr
    exit 1
fi

echo '...End of test output'


# We need an "&& true" guard because wait will pass through the exit code from MockSysfs
# which will be 143 when the process ends with SIGTERM, even if there's no other error
wait "${SYSFS_PROCESS}" && true

if grep -F 'ERROR SUMMARY' ${VALGRIND_LOG} > /dev/null
then
    echo "✅ Valgrind log appears complete"
else
    echo "❌ Error!  Valgrind log was not fully flushed after process ${SYSFS_PROCESS} was killed.  See log: ${VALGRIND_LOG}" > /dev/stderr
    exit 1
fi

if grep -F 'All heap blocks were freed -- no leaks are possible' ${VALGRIND_LOG} > /dev/null
then
    echo "✅ Valgrind did not detect any memory leaks at runtime"
else
    echo "❌ Error!  Valgrind detected issues.  See log: ${VALGRIND_LOG}" > /dev/stderr
    exit 1
fi

if diff -u ${EXPECTED_OUTPUT} ${ACTUAL_OUTPUT}
then
    echo "✅ Actual output matches expected output.  Actual output deleted because test was successful.  Expected output = ${EXPECTED_OUTPUT}"
else
    echo "❌ Error!  Actual test output does not match expected output.  Actual output = ${ACTUAL_OUTPUT}  Expected output = ${EXPECTED_OUTPUT}" > /dev/stderr
    exit 1
fi

rm -rf ${TMP_DIR}
if [ ! -d ${TMP_DIR} ]
then
    echo "✅ Cleaned up temporary directory: ${TMP_DIR}"
else
    echo "❌ Error!  Temporary directory not fully removed: ${TMP_DIR}" > /dev/stderr
    exit 1
fi

echo "✅ Reached end of unit test fixture"
