#!/usr/bin/env bash
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
MOCK_SYSFS="$ASTL_ROOT/build/debug/bin/MockSysfs"
MOUNT_POINT="/tmp/fuse/scmi"

# Constants for startup detection
TIMEOUT=30
PATTERN_READY="eccf4f7c-d1b1-47f0-9d23-159f6d38b661"

# Basic sanity checks
[[ -x $MOCK_SYSFS ]] ||
	{
		echo "❌ MockSysfs not found or not executable at $MOCK_SYSFS" >&2
		exit 1
	}

mkdir -p "$MOUNT_POINT"

LOG_DIR="$(mktemp -d /tmp/mock_sysfs_XXXXXXXX)"
SYSFS_LOG="$LOG_DIR/sysfs.log"

echo "Logs Directory = $LOG_DIR"

echo "🚀 Launching MockSysfs..."
"$MOCK_SYSFS" -f -s "$MOUNT_POINT" &>"$SYSFS_LOG" &
SYSFS_PID=$!

# Always clean up on exit
cleanup() {
	echo "🛑 Stopping MockSysfs (PID=$SYSFS_PID)..."
	kill -SIGTERM "$SYSFS_PID" 2>/dev/null || true
	wait "$SYSFS_PID" 2>/dev/null || true
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

###############
# Demo action #
###############
SAMPLE_TEST_BIN="$ASTL_ROOT/build/debug/bin/sample_test"
if [[ ! -x $SAMPLE_TEST_BIN ]]; then
	echo "❌ Error: sample_test binary not found or not executable at $SAMPLE_TEST_BIN" >&2
	exit 1
fi

echo "🚀 Running sample_test with --immediate"

# Set environment variables and run
ASTL_LOG_LEVEL=DEBUG \
	ASTL_LOG_NAME="$LOG_DIR/demo.log" \
	"$SAMPLE_TEST_BIN" --immediate

echo "🏁 Demo complete - exiting."
