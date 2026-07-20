#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASTL_ROOT="$(dirname "$SCRIPT_DIR")"
ASTL_HOST_ARCH="$("${ASTL_ROOT}/scripts/host_arch.sh")"
BUILD_PRESET="${ASTL_BUILD_PRESET:-debug}"
BUILD_DIR="${ASTL_BUILD_DIR:-$ASTL_ROOT/build/$BUILD_PRESET/$ASTL_HOST_ARCH}"
SCMI_BRIDGE="${ASTL_SCMI_BRIDGE_BIN:-$BUILD_DIR/bin/ScmiBridge}"
MOUNT_POINT="${ASTL_SCMI_BRIDGE_MOUNT_POINT:-$HOME/tmp/scmi-bridge}"
BRIDGE_LOG="${ASTL_SCMI_BRIDGE_LOG:-$ASTL_ROOT/scmi_bridge.log}"
TIMEOUT="${ASTL_SCMI_BRIDGE_TIMEOUT:-30}"
PATTERN_READY="eccf4f7c-d1b1-47f0-9d23-159f6d38b661"
DEVICE_SPECS="${ASTL_SCMI_BRIDGE_DEVICES:-}"
BRIDGE_PID=""
BRIDGE_EXIT_STATUS=""

usage() {
	echo "Usage: $(basename "$0") [MOUNT_POINT] [LOG_FILE]"
	echo
	echo "Launch ScmiBridge over SCMI telemetry ioctl devices."
	echo
	echo "Environment overrides:"
	echo "  ASTL_SCMI_BRIDGE_DEVICES      Comma-separated PATH[:tlm-N] device specifications"
	echo "  ASTL_SCMI_BRIDGE_BIN          ScmiBridge executable"
	echo "  ASTL_SCMI_BRIDGE_MOUNT_POINT  Default mount point"
	echo "  ASTL_SCMI_BRIDGE_LOG          Default log file"
	echo "  ASTL_SCMI_BRIDGE_TIMEOUT      Startup timeout in seconds"
	echo "  ASTL_BUILD_DIR                Build output directory"
	echo "  ASTL_BUILD_PRESET             Build preset (default: debug)"
}

die() {
	echo "Error: $*" >&2
	exit 1
}

discover_devices() {
	local devices=()
	local device
	local device_index
	for device in /dev/scmi/tlm_*; do
		if [[ -c $device ]]; then
			device_index="${device##*/tlm_}"
			if [[ $device_index =~ ^[0-9]+$ ]]; then
				devices+=("$device:tlm-$device_index")
			fi
		fi
	done

	# Support the device naming used by early ioctl driver revisions when the
	# current /dev/scmi/tlm_N layout is not present.
	if ((${#devices[@]} == 0)); then
		for device in /dev/scmi-tlm*; do
			if [[ -c $device ]]; then
				device_index="${device##*/scmi-tlm}"
				if [[ $device_index =~ ^[0-9]+$ ]]; then
					devices+=("$device:tlm-$device_index")
				fi
			fi
		done
	fi

	((${#devices[@]} > 0)) ||
		die "no SCMI telemetry ioctl devices found; set ASTL_SCMI_BRIDGE_DEVICES explicitly"

	local IFS=,
	DEVICE_SPECS="${devices[*]}"
}

validate_devices() {
	local specifications=()
	IFS=, read -ra specifications <<<"$DEVICE_SPECS"
	((${#specifications[@]} > 0)) || die "no SCMI telemetry ioctl devices were specified"

	local specification
	for specification in "${specifications[@]}"; do
		local device_path="${specification%%:*}"
		[[ -n $device_path ]] || die "invalid empty device in ASTL_SCMI_BRIDGE_DEVICES"
		[[ -e $device_path ]] || die "SCMI telemetry ioctl device not found: $device_path"
		[[ -r $device_path && -w $device_path ]] ||
			die "SCMI telemetry ioctl device must be readable and writable: $device_path"
	done
}

unmount_bridge() {
	if ! mountpoint -q "$MOUNT_POINT"; then
		return
	fi

	if command -v fusermount3 >/dev/null 2>&1; then
		fusermount3 -u "$MOUNT_POINT" 2>/dev/null || true
	elif command -v fusermount >/dev/null 2>&1; then
		fusermount -u "$MOUNT_POINT" 2>/dev/null || true
	else
		umount "$MOUNT_POINT" 2>/dev/null || true
	fi
}

cleanup_failed_launch() {
	if [[ -n $BRIDGE_PID ]]; then
		if kill -0 "$BRIDGE_PID" 2>/dev/null; then
			kill -SIGTERM "$BRIDGE_PID" 2>/dev/null || true
		fi
		wait "$BRIDGE_PID" 2>/dev/null || BRIDGE_EXIT_STATUS=$?
	fi
	unmount_bridge
}

report_launch_failure() {
	if [[ -n $BRIDGE_EXIT_STATUS ]]; then
		if ((BRIDGE_EXIT_STATUS > 128)); then
			echo "ScmiBridge terminated from signal $((BRIDGE_EXIT_STATUS - 128))." >&2
		else
			echo "ScmiBridge exited with status $BRIDGE_EXIT_STATUS." >&2
		fi
	fi

	echo "ScmiBridge log ($BRIDGE_LOG):" >&2
	if [[ -s $BRIDGE_LOG ]]; then
		tail -n 50 "$BRIDGE_LOG" >&2
	else
		echo "  <no output>" >&2
	fi
}

wait_until_ready() {
	local deadline=$((SECONDS + TIMEOUT))
	while ((SECONDS < deadline)); do
		if grep -qF "$PATTERN_READY" "$BRIDGE_LOG" 2>/dev/null; then
			return
		fi
		if ! kill -0 "$BRIDGE_PID" 2>/dev/null; then
			return 1
		fi
		sleep 0.2
	done
	return 1
}

main() {
	if [[ ${1:-} == -h || ${1:-} == --help ]]; then
		usage
		return
	fi
	(($# <= 2)) || die "too many arguments; use --help for usage"

	if (($# >= 1)); then
		MOUNT_POINT="$1"
	fi
	if (($# == 2)); then
		BRIDGE_LOG="$2"
	fi

	[[ $TIMEOUT =~ ^[1-9][0-9]*$ ]] || die "ASTL_SCMI_BRIDGE_TIMEOUT must be a positive integer"
	[[ -x $SCMI_BRIDGE ]] ||
		die "ScmiBridge not found or not executable at $SCMI_BRIDGE; build target ScmiBridge first"
	[[ -r /dev/fuse && -w /dev/fuse ]] || die "/dev/fuse is unavailable or lacks read/write permission"

	mkdir -p "$MOUNT_POINT" "$(dirname "$BRIDGE_LOG")"
	MOUNT_POINT="$(realpath "$MOUNT_POINT")"
	BRIDGE_LOG="$(realpath -m "$BRIDGE_LOG")"
	mountpoint -q "$MOUNT_POINT" &&
		die "$MOUNT_POINT is already mounted; run cleanup_scmi_bridge.sh first"

	if [[ -z $DEVICE_SPECS ]]; then
		discover_devices
	fi
	validate_devices

	echo "ASTL_ROOT = $ASTL_ROOT"
	echo "ASTL_BUILD_PRESET = $BUILD_PRESET"
	echo "BUILD_DIR = $BUILD_DIR"
	echo "SCMI_BRIDGE = $SCMI_BRIDGE"
	echo "SCMI devices = $DEVICE_SPECS"
	echo "Mount point = $MOUNT_POINT"
	echo "Log file = $BRIDGE_LOG"

	echo "Launching ScmiBridge..."
	nohup "$SCMI_BRIDGE" --devices "$DEVICE_SPECS" -f -s "$MOUNT_POINT" >"$BRIDGE_LOG" 2>&1 &
	BRIDGE_PID=$!
	trap 'cleanup_failed_launch; exit 130' INT TERM

	if ! wait_until_ready; then
		cleanup_failed_launch
		report_launch_failure
		die "ScmiBridge did not become ready; see $BRIDGE_LOG"
	fi

	if ! kill -0 "$BRIDGE_PID" 2>/dev/null; then
		cleanup_failed_launch
		report_launch_failure
		die "ScmiBridge exited unexpectedly; see $BRIDGE_LOG"
	fi

	local telemetry_root="$MOUNT_POINT/arm_telemetry"
	local targets=("$telemetry_root"/tlm-*)
	if [[ ! -d $telemetry_root || ! -d ${targets[0]} ]]; then
		cleanup_failed_launch
		report_launch_failure
		die "ScmiBridge mounted without exposing a telemetry target; see $BRIDGE_LOG"
	fi

	trap - INT TERM
	echo "ScmiBridge is ready (PID=$BRIDGE_PID)."
	echo "Telemetry root: $telemetry_root"
	echo
	echo "Configure ASTL with:"
	echo "  export ASTL_SCMI_INTERFACE=sysfs"
	echo "  export ASTL_SCMI_SYSFS_TELEMETRY_ROOT=$telemetry_root"
	echo
	echo "Stop the bridge with:"
	echo "  $SCRIPT_DIR/cleanup_scmi_bridge.sh '$MOUNT_POINT'"
}

main "$@"
