#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
ASTL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
FUZZER="$ASTL_ROOT/build/fuzz/arm64/bin/astl_discovery_fuzzer"
MOCK_SCMI="$ASTL_ROOT/build/fuzz/arm64/bin/MockScmi"
RUN_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/astl-discovery-fuzzer.XXXXXX")"
MOUNT_POINT="$RUN_ROOT/mock-scmi"
SCMI_LOG="$RUN_ROOT/mock-scmi.log"

cleanup() {
	"$ASTL_ROOT/tools/mock_scmi/scripts/cleanup_mockscmi.sh" "$MOUNT_POINT" >/dev/null 2>&1 || true
	rm -rf "$RUN_ROOT"
}
trap cleanup EXIT

if [[ ! -x $FUZZER || ! -x $MOCK_SCMI ]]; then
	echo "Build the fuzz preset and astl_discovery_fuzzer target first." >&2
	exit 1
fi
if (($# == 0)); then
	echo "Usage: $0 <corpus-or-input> [libFuzzer options...]" >&2
	exit 2
fi

mkdir -p "$MOUNT_POINT"
MOCK_SCMI_BIN="$MOCK_SCMI" MOCK_SCMI_TLM_JSON_PATH="$ASTL_ROOT/tools/mock_scmi/config/tlm.json" \
	"$ASTL_ROOT/tools/mock_scmi/scripts/launch_mockscmi.sh" "$MOUNT_POINT" "$SCMI_LOG" >/dev/null
export ASTL_SCMI_SYSFS_TELEMETRY_ROOT="$MOUNT_POINT/arm_telemetry"

"$FUZZER" "$@"
