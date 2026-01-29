#!/usr/bin/env bash
########################################
# run_e2e.sh
# Launch MockSysfs and run E2E tests
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
LAUNCH_MOCKSYSFS_SCRIPT="$ASTL_ROOT/scripts/launch_mocksysfs.sh"
CLEANUP_MOCKSYSFS_SCRIPT="$ASTL_ROOT/scripts/cleanup_mocksysfs.sh"
MOUNT_POINT="$HOME/tmp/fuse"
TELEMETRY_ROOT="$MOUNT_POINT/arm_telemetry"

echo "========================================"
echo "ASTL E2E Test Runner ($BUILD_TYPE)"
echo "========================================"

########################################
# Check prerequisites                  #
########################################
echo "[1/5] Checking prerequisites..."

if [[ ! -f $LAUNCH_MOCKSYSFS_SCRIPT ]]; then
	echo "❌ launch_mocksysfs.sh not found at: $LAUNCH_MOCKSYSFS_SCRIPT"
	exit 1
fi

if [[ ! -f $CLEANUP_MOCKSYSFS_SCRIPT ]]; then
	echo "❌ cleanup_mocksysfs.sh not found at: $CLEANUP_MOCKSYSFS_SCRIPT"
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
./scripts/publish_configs.sh -o "$BUILD_DIR/lib/config" --confidential --mocksysfs
echo ""

########################################
# Check if MockSysfs is already running#
########################################
echo "[2/5] Checking MockSysfs status..."

ALREADY_RUNNING=false
if pgrep -x MockSysfs >/dev/null; then
	echo "✓ MockSysfs is already running"
	ALREADY_RUNNING=true

	# Verify mount point is accessible
	if [[ ! -d $TELEMETRY_ROOT ]]; then
		echo "❌ MockSysfs running but $TELEMETRY_ROOT not accessible"
		echo "💡 Try: killall MockSysfs && sleep 2"
		exit 1
	fi
else
	echo "MockSysfs not running, will start it"
fi
echo ""

########################################
# Launch MockSysfs if needed           #
########################################
if [[ $ALREADY_RUNNING == "false" ]]; then
	echo "[3/5] Starting MockSysfs using launch_mocksysfs.sh..."

	# Launch MockSysfs using the standard script
	cd "$ASTL_ROOT"
	bash "$LAUNCH_MOCKSYSFS_SCRIPT" &
	MOCKSYSFS_SCRIPT_PID=$!

	echo "MockSysfs launcher PID: $MOCKSYSFS_SCRIPT_PID"

	# Wait for MockSysfs to be ready by checking for GUID in log
	SYSFS_LOG="$ASTL_ROOT/sysfs.log"
	MOCKSYSFS_GUID="eccf4f7c-d1b1-47f0-9d23-159f6d38b661"
	TIMEOUT=30

	echo "⏱️  Waiting (up to ${TIMEOUT}s) for MockSysfs to be ready..."
	if timeout "$TIMEOUT" bash -c "tail -n +0 -F '$SYSFS_LOG' 2>/dev/null | grep -m1 -F '$MOCKSYSFS_GUID' >/dev/null"; then
		echo "✅ MockSysfs is ready (detected GUID)"
	else
		echo "❌ MockSysfs failed to start within ${TIMEOUT}s"
		echo "Check logs in: $SYSFS_LOG"
		exit 1
	fi

	# Find the actual MockSysfs process
	MOCKSYSFS_PID=$(pgrep -x MockSysfs || echo "")

	if [[ -z $MOCKSYSFS_PID ]]; then
		echo "❌ MockSysfs process not found"
		echo "Check logs in: $SYSFS_LOG"
		exit 1
	fi

	echo "MockSysfs PID: $MOCKSYSFS_PID"
	echo "✓ MockSysfs started successfully"

	# Verify mount point
	if [[ ! -d $TELEMETRY_ROOT ]]; then
		echo "❌ Mount point not accessible: $TELEMETRY_ROOT"
		exit 1
	fi

	echo "✓ Mount point accessible: $TELEMETRY_ROOT"

	# Setup cleanup to stop MockSysfs when script exits
	cleanup_mocksysfs() {
		echo ""
		bash "$CLEANUP_MOCKSYSFS_SCRIPT" "$MOUNT_POINT"
	}
	trap cleanup_mocksysfs EXIT
else
	echo "[3/5] Using existing MockSysfs instance"
fi
echo ""

########################################
# Set environment variables            #
########################################
echo "[4/5] Setting up environment..."
export ASTL_CONFIG_DIR="$BUILD_DIR/lib/config"
export ASTL_VERBOSE=0
echo "ASTL_CONFIG_DIR: $ASTL_CONFIG_DIR"

# force ASTL to read SCMI telemetry from our mocksysfs mount point
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
