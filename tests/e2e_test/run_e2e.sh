#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

########################################
# run_e2e.sh
# Launch MockScmi and run E2E tests
# Usage: ./run_e2e.sh [debug|release]
########################################
set -euo pipefail

########################################
# Parse command-line arguments         #
########################################
BUILD_TYPE="${1:-debug}"

if [[ $BUILD_TYPE != "debug" && $BUILD_TYPE != "release" ]]; then
	echo "❌ Invalid build type: $BUILD_TYPE"
	echo "Usage: $0 [debug|release]"
	exit 1
fi

########################################
# Locate project root and scripts      #
########################################
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASTL_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$ASTL_ROOT/build/$BUILD_TYPE"
E2E_TEST_BIN="$BUILD_DIR/bin/multithreaded_e2e_test"
LAUNCH_MOCKSCMI_SCRIPT="$ASTL_ROOT/scripts/launch_mockscmi.sh"
CLEANUP_MOCKSCMI_SCRIPT="$ASTL_ROOT/scripts/cleanup_mockscmi.sh"
MOUNT_POINT="$HOME/tmp/fuse"
TELEMETRY_ROOT="$MOUNT_POINT/arm_telemetry"

echo "========================================"
echo "ASTL E2E Test Runner ($BUILD_TYPE)"
echo "========================================"

########################################
# Check prerequisites                  #
########################################
echo "[1/5] Checking prerequisites..."

if [[ ! -f $LAUNCH_MOCKSCMI_SCRIPT ]]; then
	echo "❌ launch_mockscmi.sh not found at: $LAUNCH_MOCKSCMI_SCRIPT"
	exit 1
fi

if [[ ! -f $CLEANUP_MOCKSCMI_SCRIPT ]]; then
	echo "❌ cleanup_mockscmi.sh not found at: $CLEANUP_MOCKSCMI_SCRIPT"
	exit 1
fi

if [[ ! -x $E2E_TEST_BIN ]]; then
	echo "❌ E2E test binary not found at: $E2E_TEST_BIN"
	echo "💡 Build it first: cmake --build $BUILD_DIR --target multithreaded_e2e_test"
	exit 1
fi

echo "✓ Prerequisites OK"
echo ""

###############################################################
# Copy metrics + scmi spec config/ directory to build directory #
###############################################################
echo "Publishing config files..."
./scripts/publish_configs.sh -o "$BUILD_DIR/lib/config" --confidential
echo ""

########################################
# Check if MockScmi is already running#
########################################
echo "[2/5] Checking MockScmi status..."

ALREADY_RUNNING=false
if pgrep -x MockScmi >/dev/null; then
	echo "✓ MockScmi is already running"
	ALREADY_RUNNING=true

	# Verify mount point is accessible
	if [[ ! -d $TELEMETRY_ROOT ]]; then
		echo "❌ MockScmi running but $TELEMETRY_ROOT not accessible"
		echo "💡 Try: killall MockScmi && sleep 2"
		exit 1
	fi
else
	echo "MockScmi not running, will start it"
fi
echo ""

########################################
# Launch MockScmi if needed           #
########################################
if [[ $ALREADY_RUNNING == "false" ]]; then
	echo "[3/5] Starting MockScmi using launch_mockscmi.sh..."

	# Launch MockScmi using the standard script
	cd "$ASTL_ROOT"
	bash "$LAUNCH_MOCKSCMI_SCRIPT" &
	MOCKSCMI_SCRIPT_PID=$!

	echo "MockScmi launcher PID: $MOCKSCMI_SCRIPT_PID"

	# Wait for MockScmi to be ready by checking for GUID in log
	SCMI_LOG="$ASTL_ROOT/mock_scmi.log"
	MOCKSCMI_GUID="eccf4f7c-d1b1-47f0-9d23-159f6d38b661"
	TIMEOUT=30

	echo "⏱️  Waiting (up to ${TIMEOUT}s) for MockScmi to be ready..."
	if timeout "$TIMEOUT" bash -c "tail -n +0 -F '$SCMI_LOG' 2>/dev/null | grep -m1 -F '$MOCKSCMI_GUID' >/dev/null"; then
		echo "✅ MockScmi is ready (detected GUID)"
	else
		echo "❌ MockScmi failed to start within ${TIMEOUT}s"
		echo "Check logs in: $SCMI_LOG"
		exit 1
	fi

	# Find the actual MockScmi process
	MOCKSCMI_PID=$(pgrep -x MockScmi || echo "")

	if [[ -z $MOCKSCMI_PID ]]; then
		echo "❌ MockScmi process not found"
		echo "Check logs in: $SCMI_LOG"
		exit 1
	fi

	echo "MockScmi PID: $MOCKSCMI_PID"
	echo "✓ MockScmi started successfully"

	# Verify mount point
	if [[ ! -d $TELEMETRY_ROOT ]]; then
		echo "❌ Mount point not accessible: $TELEMETRY_ROOT"
		exit 1
	fi

	echo "✓ Mount point accessible: $TELEMETRY_ROOT"

	# Setup cleanup to stop MockScmi when script exits
	# shellcheck disable=SC2329
	cleanup_mockscmi() {
		# used indirectly in trap, so ignore 'unreachable' warnings
		# shellcheck disable=2317
		echo ""
		# shellcheck disable=2317
		bash "$CLEANUP_MOCKSCMI_SCRIPT" "$MOUNT_POINT"
	}
	trap cleanup_mockscmi EXIT
else
	echo "[3/5] Using existing MockScmi instance"
fi
echo ""

########################################
# Set environment variables            #
########################################
echo "[4/5] Setting up environment..."
export ASTL_CONFIG_DIR="$BUILD_DIR/lib/config"
export ASTL_VERBOSE=0
echo "ASTL_CONFIG_DIR: $ASTL_CONFIG_DIR"

# force ASTL to read SCMI telemetry from our MockScmi mount point
export ASTL_SCMI_SYSFS_TELEMETRY_ROOT="$TELEMETRY_ROOT"
echo "ASTL_SCMI_SYSFS_TELEMETRY_ROOT: $ASTL_SCMI_SYSFS_TELEMETRY_ROOT"
echo ""

########################################
# Run E2E test                         #
########################################
echo "[5/5] Running E2E test..."
echo "========================================"
echo ""

# Run the test and capture exit code
set +e
"$E2E_TEST_BIN" "$TELEMETRY_ROOT"
TEST_EXIT_CODE=$?
set -e

echo ""
echo "========================================"

if [[ $TEST_EXIT_CODE -eq 0 ]]; then
	echo "✅ E2E test PASSED"
else
	echo "❌ E2E test FAILED (exit code: $TEST_EXIT_CODE)"
fi

echo "========================================"

exit $TEST_EXIT_CODE
