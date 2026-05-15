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
ASTL_HOST_ARCH="$("${ASTL_ROOT}/scripts/host_arch.sh")"
echo "ASTL_ROOT = $ASTL_ROOT"

########################################
# Launch MockSysfs (FUSE) demo         #
########################################
export ASTL_MOCKSYSFS_TLM_JSON_PATH="$ASTL_ROOT/tools/mock_sysfs/config/tlm.json"
echo "ASTL_MOCKSYSFS_TLM_JSON_PATH = $ASTL_MOCKSYSFS_TLM_JSON_PATH"
MOCK_SYSFS="$ASTL_ROOT/build/debug/${ASTL_HOST_ARCH}/bin/MockSysfs"

# Allow optional arguments to override MOUNT_POINT and SYSFS_LOG.
MOUNT_POINT="${HOME}/tmp/fuse"
SYSFS_LOG="$ASTL_ROOT/sysfs.log"
if [[ $# -gt 2 ]]; then
	echo "❌ Usage: $(basename "$0") [MOUNT_POINT] [SYSFS_LOG]" >&2
	exit 1
fi
if [[ $# -ge 1 ]]; then
	MOUNT_POINT="$1"
	if [[ ! -d $MOUNT_POINT ]]; then
		echo "📁 Mount point '$MOUNT_POINT' does not exist, creating it..."
		mkdir -p "$MOUNT_POINT" || {
			echo "❌ Failed to create mount point '$MOUNT_POINT'" >&2
			exit 1
		}
	fi
fi
if [[ $# -eq 2 ]]; then
	SYSFS_LOG="$2"
fi

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

echo "Logs Directory = $SYSFS_LOG"

echo "🚀 Launching MockSysfs..."
# Keep MockSysfs alive after this launcher script exits.
nohup "$MOCK_SYSFS" -f -s "$MOUNT_POINT" >"$SYSFS_LOG" 2>&1 &
SYSFS_PID=$!

# Note, if not mocksysfs, use
# `	mount -t stlmfs none /sys/fs/arm_telemetry/ `
# to mount the real sysfs interface

find "$MOUNT_POINT"

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

# Verify that the mounted filesystem accepts control writes.
TLM_ENABLE_FILE="$TELEMETRY_ROOT/tlm-0/tlm_enable"
if ! printf '1' >"$TLM_ENABLE_FILE"; then
	echo "❌ MockSysfs mounted, but $TLM_ENABLE_FILE is not writable" >&2
	exit 1
fi

echo "✅ MockSysfs mounted at $MOUNT_POINT"
echo "✅ Verified writable control file: $TLM_ENABLE_FILE"
echo "🧵 To stop MockSysfs (PID=$SYSFS_PID)..."
echo "kill -SIGTERM $SYSFS_PID 2>/dev/null || true"
