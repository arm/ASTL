#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

########################################
# cleanup_mockscmi.sh
# Stop MockScmi and cleanup mount point
########################################

MOUNT_POINT="${1:-$HOME/tmp/fuse}"

echo "Stopping MockScmi..."

# Find and kill MockScmi processes
PIDS=$(pgrep -x MockScmi || true)

if [[ -z $PIDS ]]; then
	echo "No MockScmi processes found"
else
	echo "Found MockScmi PIDs: $PIDS"
	for PID in $PIDS; do
		echo "Stopping PID $PID..."
		kill -SIGTERM "$PID" 2>/dev/null || true

		# Wait up to 5 seconds for graceful shutdown
		for _ in {1..10}; do
			if ! kill -0 "$PID" 2>/dev/null; then
				break
			fi
			sleep 0.5
		done

		# Force kill if still running
		if kill -0 "$PID" 2>/dev/null; then
			echo "Force killing PID $PID..."
			kill -SIGKILL "$PID" 2>/dev/null || true
		fi
	done
	echo "MockScmi processes stopped"
fi

# Unmount if still mounted (Linux only - uses fusermount)
if [[ -d $MOUNT_POINT ]] && mount | grep -q "$MOUNT_POINT"; then
	echo "Unmounting $MOUNT_POINT..."
	fusermount -u "$MOUNT_POINT" 2>/dev/null || true
fi

echo "Cleanup complete"
