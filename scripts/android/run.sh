#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
abi="${1:-arm64-v8a}"
case "$abi" in
arm64-v8a | x86_64) ;;
*)
	echo 'Usage: run.sh [arm64-v8a|x86_64]' >&2
	exit 2
	;;
esac
build_dir="$repo_root/build/android-$abi"
bundle="$build_dir/bundle"
logs="${ASTL_ANDROID_LOG_DIR:-$build_dir/test-results}"
mkdir -p "$logs"
adb_command=("${ADB:-adb}")
if [[ -n ${ANDROID_SERIAL:-} ]]; then
	adb_command+=(-s "$ANDROID_SERIAL")
fi
adb_run() { "${adb_command[@]}" "$@"; }

# Always retain diagnostics, including when deployment or procfs permissions fail.
collect_diagnostics() {
	adb_run shell 'id; id -Z; getenforce; getprop ro.build.version.sdk; getprop ro.product.cpu.abi' >"$logs/context.txt" 2>&1 || true
	for file in stat meminfo; do
		adb_run shell cat "/proc/$file" >"$logs/proc-$file.txt" 2>&1 || true
	done
	adb_run logcat -d -t 500 >"$logs/logcat.txt" 2>&1 || true
}
trap collect_diagnostics EXIT

# adb shell has broader permissions than an ordinary APK. Keep SELinux enforcing and do not request root.
# shellcheck disable=SC2016 # Expand id/getenforce in the Android shell, not on the host.
adb_run shell 'test "$(id -u)" = 2000 && test "$(getenforce)" = Enforcing'
adb_run shell 'test -r /proc/stat && test -r /proc/meminfo'
adb_run shell "mkdir -p /data/local/tmp/astl-procfs-$abi"
adb_run push "$bundle/." "/data/local/tmp/astl-procfs-$abi/" >"$logs/deploy.txt" 2>&1
adb_run shell "chmod 755 /data/local/tmp/astl-procfs-$abi/bin/procfs_smoke"
# Clear root overrides inherited from a developer shell and use the emulator's live /proc.
adb_run shell "cd /data/local/tmp/astl-procfs-$abi && unset ASTL_PROCFS_ROOT && \
  ASTL_CONFIG_DIR=\$PWD/config ASTL_COLLECTORS=procfs LD_LIBRARY_PATH=\$PWD/lib \
  ./bin/procfs_smoke" 2>&1 | tee "$logs/astl.txt"
