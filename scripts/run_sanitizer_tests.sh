#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -eu -o pipefail

PRESET="${1:-}"
case "$PRESET" in
debug-asan-ubsan | debug-tsan) ;;
*)
	echo "Usage: $0 {debug-asan-ubsan|debug-tsan}" >&2
	exit 2
	;;
esac

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RESULT_DIR="$REPO_ROOT/build/$PRESET/test-results"
LOG_DIR="$REPO_ROOT/build/$PRESET/sanitizer-logs"
mkdir -p "$RESULT_DIR" "$LOG_DIR"

if [[ $PRESET == "debug-asan-ubsan" ]]; then
	export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=1:strict_string_checks=1:check_initialization_order=1:symbolize=1:log_path=$LOG_DIR/asan"
	export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1:log_path=$LOG_DIR/ubsan"
	unset TSAN_OPTIONS
else
	export TSAN_OPTIONS="halt_on_error=1:symbolize=1:log_path=$LOG_DIR/tsan"
	unset ASAN_OPTIONS UBSAN_OPTIONS
fi

# The standalone libsensors suite supplies a mock API. All wrapper and E2E-style discovery in this run is restricted to
# SCMI so it cannot observe sensors from the host.
export ASTL_COLLECTORS="scmi"

cd "$REPO_ROOT"

# Catch2 tests are registered individually. Run tests that need process-wide isolation serially, while retaining the
# usual parallel execution for the rest. Samples and MockScmi-backed E2E tests are excluded from this deterministic
# native suite.
set +e
ctest --preset "$PRESET" --parallel 4 \
	-LE "sample|e2e|mockscmi|time_sensitive|valgrind_isolated|^valgrind$" \
	--output-junit "$RESULT_DIR/parallel.xml"
PARALLEL_STATUS=$?

ctest --preset "$PRESET" --parallel 1 \
	-L "time_sensitive|valgrind_isolated" \
	-LE "sample|e2e|mockscmi|^valgrind$" \
	--output-junit "$RESULT_DIR/serial.xml"
SERIAL_STATUS=$?
set -e

if ((PARALLEL_STATUS != 0 || SERIAL_STATUS != 0)); then
	exit 1
fi
