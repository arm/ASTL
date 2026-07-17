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
# Launch MockScmi (FUSE) demo         #
########################################
export ASTL_MOCKSCMI_TLM_JSON_PATH="$ASTL_ROOT/tools/mock_scmi/config/tlm.json"
echo "ASTL_MOCKSCMI_TLM_JSON_PATH = $ASTL_MOCKSCMI_TLM_JSON_PATH"
BUILD_PRESET="${ASTL_BUILD_PRESET:-debug}"
ASTL_HOST_ARCH="$("${ASTL_ROOT}/scripts/host_arch.sh")"
BUILD_DIR="${ASTL_BUILD_DIR:-$ASTL_ROOT/build/$BUILD_PRESET/$ASTL_HOST_ARCH}"
MOCK_SCMI="${ASTL_MOCKSCMI_BIN:-}"
if [[ -z $MOCK_SCMI && -n ${ATX_BIN_PATH:-} ]]; then
	ATX_BIN_DIR="$(dirname "$(realpath "${ATX_BIN_PATH}")")"
	ATX_ADJACENT_MOCKSCMI="${ATX_BIN_DIR}/MockScmi"
	if [[ -x $ATX_ADJACENT_MOCKSCMI ]]; then
		MOCK_SCMI="$ATX_ADJACENT_MOCKSCMI"
	fi
fi
if [[ -z $MOCK_SCMI ]]; then
	MOCK_SCMI="$BUILD_DIR/bin/MockScmi"
fi
echo "ASTL_BUILD_PRESET = $BUILD_PRESET"
echo "BUILD_DIR = $BUILD_DIR"
echo "MOCK_SCMI = $MOCK_SCMI"

# Allow optional arguments to override MOUNT_POINT and SCMI_LOG.
MOUNT_POINT="${HOME}/tmp/fuse"
SCMI_LOG="$ASTL_ROOT/mock_scmi.log"
if [[ $# -gt 2 ]]; then
	echo "❌ Usage: $(basename "$0") [MOUNT_POINT] [SCMI_LOG]" >&2
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
	SCMI_LOG="$2"
fi

# Constants for startup detection
TIMEOUT=30
PATTERN_READY="eccf4f7c-d1b1-47f0-9d23-159f6d38b661"

# Basic sanity checks
[[ -x $MOCK_SCMI ]] ||
	{
		echo "❌ MockScmi not found or not executable at $MOCK_SCMI" >&2
		exit 1
	}

TELEMETRY_ROOT="$MOUNT_POINT/arm_telemetry"
mkdir -p "$TELEMETRY_ROOT"

echo "Logs Directory = $SCMI_LOG"

echo "🚀 Launching MockScmi..."
# Keep MockScmi alive after this launcher script exits.
nohup "$MOCK_SCMI" -f -s "$MOUNT_POINT" >"$SCMI_LOG" 2>&1 &
SCMI_PID=$!

# Note, if not mockscmi, use
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
			echo '❌ Timeout waiting for MockScmi' >&2
			exit 1
		}
	echo "✅ Detected '$pattern' in $desc"
}

wait_for "$SCMI_LOG" "MockScmi startup log" "$PATTERN_READY"

# Verify that the mounted filesystem accepts control writes.
TLM_ENABLE_FILE="$TELEMETRY_ROOT/tlm-0/tlm_enable"
if ! printf '1' >"$TLM_ENABLE_FILE"; then
	echo "❌ MockScmi mounted, but $TLM_ENABLE_FILE is not writable" >&2
	exit 1
fi

echo "✅ MockScmi mounted at $MOUNT_POINT"
echo "✅ Verified writable control file: $TLM_ENABLE_FILE"
echo "🧵 To stop MockScmi (PID=$SCMI_PID)..."
echo "kill -SIGTERM $SCMI_PID 2>/dev/null || true"
