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

# Exit if lm-sensors package not installed
if ! command -v sensors &>/dev/null; then
	echo "❌ lm-sensors package not found. Please install it and try again." >&2
	exit 1
fi

# Default mode duration and interval
# Default to interval mode with 10 seconds duration and 500ms interval
# unless overridden by command-line arguments
MODE="interval"
DURATION=10
INTERVAL=1000

# Parse command-line arguments for mode, interval, and duration (using '=' syntax)
while [[ $# -gt 0 ]]; do
	case "$1" in
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

LOG_DIR="$ASTL_ROOT"
echo "Logs Directory = $LOG_DIR"

###############
# Demo action #
###############
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

"$SAMPLE_TEST_BIN" "${RUN_ARGS[@]}" -target="libsensors"
ERR=$?
if [[ $ERR -ne 0 ]]; then
	echo "❌ Error: $SAMPLE_TEST_BIN returned a non-zero return code $ERR" >&2
	exit $ERR
fi

echo "🏁 Demo complete - exiting."
