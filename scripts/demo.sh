#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

########################################
# Locate the project root (ASTL_ROOT)  #
########################################
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASTL_ROOT="$(dirname "${SCRIPT_DIR}")"
ASTL_HOST_ARCH="$("${ASTL_ROOT}/scripts/host_arch.sh")"
echo "ASTL_ROOT = ${ASTL_ROOT}"

########################################
# Launch MockSysfs (FUSE) demo         #
########################################
export ASTL_MOCKSYSFS_TLM_JSON_PATH="${ASTL_ROOT}/tools/mock_sysfs/config/tlm.json"
echo "ASTL_MOCKSYSFS_TLM_JSON_PATH = ${ASTL_MOCKSYSFS_TLM_JSON_PATH}"
BUILD_PRESET="${ASTL_BUILD_PRESET:-debug}"
BUILD_DIR="${ASTL_BUILD_DIR:-${ASTL_ROOT}/build/${BUILD_PRESET}/${ASTL_HOST_ARCH}}"
MOCK_SYSFS="${ASTL_MOCKSYSFS_BIN:-${BUILD_DIR}/bin/MockSysfs}"
echo "ASTL_BUILD_PRESET = ${BUILD_PRESET}"
echo "BUILD_DIR = ${BUILD_DIR}"
echo "MOCK_SYSFS = ${MOCK_SYSFS}"
MOUNT_POINT="${ASTL_MOCKSYSFS_MOUNT_POINT:-${TMPDIR:-/tmp}/astl-mocksysfs}"

# Constants for startup detection
TIMEOUT=30
PATTERN_READY="eccf4f7c-d1b1-47f0-9d23-159f6d38b661"

# Basic sanity checks
[[ -x ${MOCK_SYSFS} ]] ||
	{
		echo "❌ MockSysfs not found or not executable at ${MOCK_SYSFS}" >&2
		exit 1
	}

TELEMETRY_ROOT="${MOUNT_POINT}/arm_telemetry"

# Default mode duration and interval
# Default to interval mode with 10 seconds duration and 500ms interval
# unless overridden by command-line arguments
GROUP=""
MODE="interval"
DURATION=10
INTERVAL=500
SAVE_PATH=""
LOAD_PATH=""
TARGET_NAME="${SCMI_TLM_CHIP_TARGET:-scmi-mocksysfs-tlm-0}"

# Parse command-line arguments for mode, interval, and duration (using '=' syntax)
while [[ $# -gt 0 ]]; do
	case "$1" in
	--group=*)
		GROUP="${1#--group=}"
		shift
		;;
	--immediate)
		MODE="immediate"
		shift
		;;
	--interval=*)
		MODE="interval"
		INTERVAL="${1#--interval=}"
		shift
		;;
	--duration=*)
		DURATION="${1#--duration=}"
		shift
		;;
	--save=*)
		SAVE_PATH="${1#--save=}"
		shift
		;;
	--load=*)
		LOAD_PATH="${1#--load=}"
		shift
		;;
	*)
		break
		;;
	esac
done

mkdir -p "${MOUNT_POINT}"

LOG_DIR="${ASTL_ROOT}"
SYSFS_LOG="${LOG_DIR}/sysfs.log"

echo "Logs Directory = ${LOG_DIR}"

echo "🚀 Launching MockSysfs..."
"${MOCK_SYSFS}" -f -s "${MOUNT_POINT}" &>"${SYSFS_LOG}" &
SYSFS_PID=$!

# Note, if not mocksysfs, use
# `	mount -t stlmfs none /sys/fs/arm_telemetry/ `
# to mount the real sysfs interface

# Always clean up on exit
cleanup() {
	echo "🛑 Stopping MockSysfs (PID=${SYSFS_PID})..."
	kill -SIGTERM "${SYSFS_PID}" 2>/dev/null || true
	wait "${SYSFS_PID}" 2>/dev/null || true
	if mount | grep -q "on ${MOUNT_POINT} "; then
		fusermount3 -u "${MOUNT_POINT}" 2>/dev/null || fusermount -u "${MOUNT_POINT}" 2>/dev/null || true
	fi
}
trap cleanup EXIT

########################################
# Helper: wait for GUID in the log     #
########################################
wait_for() {
	local file=${1} desc=${2} pattern=${3}
	echo "⏱️  Waiting (up to ${TIMEOUT}s) for '${pattern}' in ${desc}..."
	timeout "${TIMEOUT}" bash -c \
		"stdbuf -oL tail -n +0 -F '${file}' | grep -m1 -F '${pattern}'" ||
		{
			echo '❌ Timeout waiting for MockSysfs' >&2
			exit 1
		}
	echo "✅ Detected '${pattern}' in ${desc}"
}

wait_for "${SYSFS_LOG}" "MockSysfs startup log" "${PATTERN_READY}"
echo "✅ MockSysfs mounted at ${MOUNT_POINT}"

###############################################################
# Copy metrics + scmi spec config/ directory to build directory #
###############################################################
./scripts/publish_configs.sh -o "${BUILD_DIR}/lib/config" --confidential --mocksysfs

###############
# Demo action #
###############

### delete tmp/*.astl files if they exist to avoid interference with old samples
rm -f tmp/*.astl

SAMPLE_TEST_BIN="${BUILD_DIR}/bin/sample_test"
if [[ ! -x ${SAMPLE_TEST_BIN} ]]; then
	echo "❌ Error: sample_test binary not found or not executable at ${SAMPLE_TEST_BIN}" >&2
	exit 1
fi

# Run sample_test in selected mode
if [[ ${MODE} == "immediate" ]]; then
	echo "🚀 Running sample_test with --immediate"
	RUN_ARGS=(--immediate)
else
	echo "🚀 Running sample_test with --interval for ${DURATION}s"
	RUN_ARGS=(--interval="${INTERVAL}" --duration="${DURATION}")
fi

if [[ -n ${GROUP} ]]; then
	echo "🚀 Using metric group: ${GROUP}"
	RUN_ARGS+=(--group="${GROUP}")
fi

if [[ -n ${SAVE_PATH} ]]; then
	echo "💾 Will save session to: ${SAVE_PATH}"
	RUN_ARGS+=(--save="${SAVE_PATH}")
fi

# force ASTL to use our mocksysfs mount point for the SCMI sysfs rather than the default /sys/fs/arm_telemetry
export ASTL_SCMI_SYSFS_TELEMETRY_ROOT="${TELEMETRY_ROOT}"
export ASTL_CONFIG_DIR="${BUILD_DIR}/lib/config"
echo "ASTL_CONFIG_DIR = ${ASTL_CONFIG_DIR}"

# Set CSV output file for summary data
export ASTL_OUTPUT_SUMMARY_CSV="${LOG_DIR}/astl_summary.csv"
echo "CSV output will be written to: ${ASTL_OUTPUT_SUMMARY_CSV}"

export ASTL_OUTPUT_SUMMARY_CSV="${LOG_DIR}/astl_summary.csv"
echo "CSV output will be written to: ${ASTL_OUTPUT_SUMMARY_CSV}"

echo "🚀 Executing sample_test"
echo "${SAMPLE_TEST_BIN}" "${RUN_ARGS[@]}" --target="${TARGET_NAME}"
"${SAMPLE_TEST_BIN}" "${RUN_ARGS[@]}" --target="${TARGET_NAME}"
ERR=$?
if [[ ${ERR} -ne 0 ]]; then
	echo "❌ Error: ${SAMPLE_TEST_BIN} returned a non-zero return code ${ERR}" >&2
	exit "${ERR}"
fi

# If a save path was provided (or a load path given directly), demonstrate loading the saved session
if [[ -n ${LOAD_PATH} ]]; then
	echo "📂 Loading session from: ${LOAD_PATH}"
	echo "${SAMPLE_TEST_BIN}" --load="${LOAD_PATH}" --target="${TARGET_NAME}"
	"${SAMPLE_TEST_BIN}" --load="${LOAD_PATH}" --target="${TARGET_NAME}"
	LOAD_ERR=$?
	if [[ ${LOAD_ERR} -ne 0 ]]; then
		echo "❌ Error: sample_test --load returned non-zero code ${LOAD_ERR}" >&2
		exit "${LOAD_ERR}"
	fi
	echo "✅ Session loaded successfully from ${LOAD_PATH}"
elif [[ -n ${SAVE_PATH} ]]; then
	echo "📂 Re-loading saved session from: ${SAVE_PATH}"
	echo "${SAMPLE_TEST_BIN}" --load="${SAVE_PATH}" --target="${TARGET_NAME}"
	"${SAMPLE_TEST_BIN}" --load="${SAVE_PATH}" --target="${TARGET_NAME}"
	LOAD_ERR=$?
	if [[ ${LOAD_ERR} -ne 0 ]]; then
		echo "❌ Error: sample_test --load returned non-zero code ${LOAD_ERR}" >&2
		exit "${LOAD_ERR}"
	fi
	echo "✅ Session round-trip (save → load) succeeded"
fi

echo "🏁 Demo complete - exiting."
