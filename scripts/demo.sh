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

# Default mode duration and interval
# Default to interval mode with 10 seconds duration and 500ms interval
# unless overridden by command-line arguments
GROUP=""
MODE="interval"
DURATION=10
INTERVAL=500

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
	*)
		break
		;;
	esac
done

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

###############################################################
# Copy metrics + scmi spec config/ directory to build directory #
###############################################################
./scripts/publish_configs.sh -o "$ASTL_ROOT/build/debug/lib/config" --confidential --mocksysfs

###############
# Demo action #
###############

### delete tmp/*.astl files if they exist to avoid interference with old samples
rm -f tmp/*.astl

SAMPLE_TEST_BIN="$ASTL_ROOT/build/debug/bin/sample_test"
if [[ ! -x $SAMPLE_TEST_BIN ]]; then
	echo "❌ Error: sample_test binary not found or not executable at $SAMPLE_TEST_BIN" >&2
	exit 1
fi

# Run sample_test in selected mode
if [[ $MODE == "immediate" ]]; then
	echo "🚀 Running sample_test with --immediate"
	RUN_ARGS=(--immediate)
else
	echo "🚀 Running sample_test with --interval for ${DURATION}s"
	RUN_ARGS=(--interval="$INTERVAL" --duration="$DURATION")
fi

if [[ -n $GROUP ]]; then
	echo "🚀 Using metric group: $GROUP"
	RUN_ARGS+=(--group="$GROUP")
fi

# Note that ASTL_CONFIG_JSON_PATH is an internal-use-only environment variable
# meant to manually force the path ASTL uses for its configuration file.
# Instead of auto-detecting it using the .so path.
export ASTL_CONFIG_JSON_PATH=~/tmp/updated_config.json
echo "ASTL_CONFIG_JSON_PATH = ${ASTL_CONFIG_JSON_PATH}"
# Configure Scmi collector to look in the mounted MockSysfs telemetry path
# rather than the production Scmi sysfs path.
jq --arg telemetry_root "$TELEMETRY_ROOT" \
	'.scmi_sysfs_telemetry_root_path = $telemetry_root' \
	./samples/sample_configuration/astl_configuration.json >$ASTL_CONFIG_JSON_PATH

# Set CSV output file for summary data
export ASTL_OUTPUT_SUMMARY_CSV="$LOG_DIR/astl_summary.csv"
echo "CSV output will be written to: $ASTL_OUTPUT_SUMMARY_CSV"

export ASTL_OUTPUT_SUMMARY_CSV="$LOG_DIR/astl_summary.csv"
echo "CSV output will be written to: $ASTL_OUTPUT_SUMMARY_CSV"

echo "🚀 Executing sample_test"
echo "$SAMPLE_TEST_BIN" "${RUN_ARGS[@]}" --target="tlm-0"
"$SAMPLE_TEST_BIN" "${RUN_ARGS[@]}" --target="tlm-0"
ERR=$?
if [[ $ERR -ne 0 ]]; then
	echo "❌ Error: $SAMPLE_TEST_BIN returned a non-zero return code $ERR" >&2
	exit $ERR
fi

echo "🏁 Demo complete - exiting."
