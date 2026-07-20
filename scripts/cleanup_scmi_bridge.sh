#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

MOUNT_POINT="${ASTL_SCMI_BRIDGE_MOUNT_POINT:-$HOME/tmp/scmi-bridge}"

usage() {
	echo "Usage: $(basename "$0") [MOUNT_POINT]"
	echo
	echo "Stop ScmiBridge processes associated with MOUNT_POINT and unmount the FUSE tree."
}

process_uses_mountpoint() {
	local pid=$1
	local argument
	while IFS= read -r -d '' argument; do
		if [[ $argument == "$MOUNT_POINT" ]]; then
			return 0
		fi
	done <"/proc/$pid/cmdline"
	return 1
}

unmount_bridge() {
	if ! mountpoint -q "$MOUNT_POINT"; then
		return
	fi

	echo "Unmounting $MOUNT_POINT..."
	if command -v fusermount3 >/dev/null 2>&1; then
		fusermount3 -u "$MOUNT_POINT"
	elif command -v fusermount >/dev/null 2>&1; then
		fusermount -u "$MOUNT_POINT"
	else
		umount "$MOUNT_POINT"
	fi
}

main() {
	if [[ ${1:-} == -h || ${1:-} == --help ]]; then
		usage
		return
	fi
	(($# <= 1)) || {
		echo "Error: too many arguments; use --help for usage" >&2
		return 1
	}

	if (($# == 1)); then
		MOUNT_POINT="$1"
	fi
	MOUNT_POINT="$(realpath -m "$MOUNT_POINT")"

	local pids=()
	local pid
	while IFS= read -r pid; do
		if [[ -n $pid && -r /proc/$pid/cmdline ]] && process_uses_mountpoint "$pid"; then
			pids+=("$pid")
		fi
	done < <(pgrep -x ScmiBridge || true)

	if ((${#pids[@]} == 0)); then
		echo "No ScmiBridge process found for $MOUNT_POINT."
	else
		echo "Stopping ScmiBridge PIDs: ${pids[*]}"
		for pid in "${pids[@]}"; do
			kill -SIGTERM "$pid" 2>/dev/null || true
		done

		for pid in "${pids[@]}"; do
			for _ in {1..10}; do
				if ! kill -0 "$pid" 2>/dev/null; then
					break
				fi
				sleep 0.5
			done
			if kill -0 "$pid" 2>/dev/null; then
				echo "Force killing PID $pid..."
				kill -SIGKILL "$pid" 2>/dev/null || true
			fi
		done
	fi

	unmount_bridge
	echo "ScmiBridge cleanup complete."
}

main "$@"
