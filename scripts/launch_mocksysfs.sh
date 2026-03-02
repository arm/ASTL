#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

########################################
# Locate the project root (ASTL_ROOT)  #
########################################
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASTL_ROOT="$(dirname "$SCRIPT_DIR")"
echo "ASTL_ROOT = $ASTL_ROOT"

########################################
# Launch MockSysfs (FUSE) demo         #
########################################
export ASTL_MOCKSYSFS_TLM_JSON_PATH="$ASTL_ROOT/tools/mock_sysfs/config/tlm.json"
echo "ASTL_MOCKSYSFS_TLM_JSON_PATH = $ASTL_MOCKSYSFS_TLM_JSON_PATH"
MOCK_SYSFS="$ASTL_ROOT/build/debug/bin/MockSysfs"
MOUNT_POINT=~/tmp/fuse

# Constants for startup detection
TIMEOUT=30
PATTERN_READY="eccf4f7c-d1b1-47f0-9d23-159f6d38b661"

# Basic sanity checks
[[ -x $MOCK_SYSFS ]] ||
	{
		echo "❌ MockSysfs not found or not executable at $MOCK_SYSFS" >&2
		exit 1
	}

TELEMETRY_ROOT="$MOUNT_POINT/arm_telemetry"
mkdir -p "$TELEMETRY_ROOT"

LOG_DIR="$ASTL_ROOT"
SYSFS_LOG="$LOG_DIR/sysfs.log"
echo "Logs Directory = $LOG_DIR"

echo "🚀 Launching MockSysfs..."
"$MOCK_SYSFS" -f -s "$MOUNT_POINT" &>"$SYSFS_LOG" &
SYSFS_PID=$!

# Note, if not mocksysfs, use
# `	mount -t stlmfs none /sys/fs/arm_telemetry/ `
# to mount the real sysfs interface

find "$MOUNT_POINT"

# Always clean up on exit
cleanup() {
	echo "🛑 To stop MockSysfs (PID=$SYSFS_PID)..."
	echo "kill -SIGTERM $SYSFS_PID 2>/dev/null || true"
}
trap cleanup EXIT

########################################
# Helper: wait for GUID in the log     #
########################################
wait_for() {
	local file=$1 desc=$2 pattern=$3
	echo "⏱️  Waiting (up to ${TIMEOUT}s) for '$pattern' in $desc..."
	timeout "$TIMEOUT" bash -c \
		"stdbuf -oL tail -n +0 -F '$file' | grep -m1 -F '$pattern'" ||
		{
			echo '❌ Timeout waiting for MockSysfs' >&2
			exit 1
		}
	echo "✅ Detected '$pattern' in $desc"
}

wait_for "$SYSFS_LOG" "MockSysfs startup log" "$PATTERN_READY"
echo "✅ MockSysfs mounted at $MOUNT_POINT"
