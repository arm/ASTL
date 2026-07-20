#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

readonly TELEMETRY_MODULE="scmi_system_telemetry"
readonly TELEMETRY_ROOT="/sys/fs/arm_telemetry"
readonly IOCTL_DEVICE_ROOT="/dev/scmi"

INTERFACE="auto"

usage() {
	cat <<EOF
Usage: $(basename "$0") [--interface auto|sysfs|ioctl]

Prepare ARM AGI CPU telemetry using the SCMI telemetry ioctl character-device
interface when available, otherwise use the legacy sysfs interface.

Options:
  -i, --interface INTERFACE  Override automatic selection with "sysfs" or
                             "ioctl" (default: "auto").
  -h, --help                 Show this help text.
EOF
}

die() {
	echo "Error: $*" >&2
	exit 1
}

parse_args() {
	while (($# > 0)); do
		case "$1" in
		-i | --interface)
			(($# >= 2)) || die "$1 requires an argument"
			INTERFACE="$2"
			shift 2
			;;
		--interface=*)
			INTERFACE="${1#*=}"
			shift
			;;
		-h | --help)
			usage
			exit 0
			;;
		*)
			die "unknown argument '$1'; use --help for usage"
			;;
		esac
	done

	case "${INTERFACE}" in
	auto | sysfs | ioctl) ;;
	*) die "unsupported interface '${INTERFACE}'; expected auto, sysfs, or ioctl" ;;
	esac
}

load_telemetry_driver() {
	echo "Loading ${TELEMETRY_MODULE}..."
	if ! modprobe "${TELEMETRY_MODULE}"; then
		die "could not load ${TELEMETRY_MODULE}; ensure CONFIG_ARM_SCMI_SYSTEM_TELEMETRY is enabled"
	fi
}

wait_for_telemetry_devices() {
	if command -v udevadm >/dev/null 2>&1; then
		udevadm settle --timeout=10 || echo "Warning: Timed out waiting for udev" >&2
	fi
}

select_interface() {
	if [[ ${INTERFACE} != auto ]]; then
		return
	fi

	local device
	for device in "${IOCTL_DEVICE_ROOT}"/tlm_*; do
		if [[ -c ${device} ]]; then
			INTERFACE="ioctl"
			echo "Detected SCMI telemetry ioctl interface."
			return
		fi
	done

	INTERFACE="sysfs"
	echo "SCMI telemetry ioctl devices not found; using the legacy sysfs interface."
}

mount_sysfs_interface() {
	if ! mountpoint -q "${TELEMETRY_ROOT}"; then
		echo "Mounting stlmfs at ${TELEMETRY_ROOT}..."
		mkdir -p "${TELEMETRY_ROOT}"
		mount -t stlmfs none "${TELEMETRY_ROOT}"
		return
	fi

	echo "stlmfs is already mounted at ${TELEMETRY_ROOT}."
	if [[ ! -d ${TELEMETRY_ROOT}/tlm_0 && ! -d ${TELEMETRY_ROOT}/tlm_1 ]]; then
		echo "Telemetry devices not found. Remounting ${TELEMETRY_ROOT}..."
		umount "${TELEMETRY_ROOT}"
		mount -t stlmfs none "${TELEMETRY_ROOT}"
	fi
}

setup_sysfs_interface() {
	mount_sysfs_interface

	chmod -R 777 "${TELEMETRY_ROOT}" ||
		echo "Warning: Could not change permissions on ${TELEMETRY_ROOT}" >&2

	for tlm in tlm_0 tlm_1; do
		local tlm_dir="${TELEMETRY_ROOT}/${tlm}"
		if [[ -d ${tlm_dir} ]]; then
			echo "Enabling telemetry on ${tlm}..."
			echo y >"${tlm_dir}/tlm_enable"
			echo y >"${tlm_dir}/all_des_enable"
		else
			echo "Warning: ${tlm_dir} not found, skipping ${tlm}." >&2
		fi
	done
}

setup_ioctl_interface() {
	# device_create() publishes the character devices through devtmpfs; unlike
	# the legacy interface, there is no filesystem to mount.
	local devices=()
	local device
	for device in "${IOCTL_DEVICE_ROOT}"/tlm_*; do
		if [[ -c ${device} ]]; then
			devices+=("${device}")
		fi
	done

	((${#devices[@]} > 0)) ||
		die "no SCMI telemetry ioctl devices found under ${IOCTL_DEVICE_ROOT}"

	echo "SCMI telemetry ioctl devices: ${devices[*]}"
	chmod 666 "${devices[@]}" ||
		echo "Warning: Could not change permissions on SCMI telemetry ioctl devices" >&2

	# ASTL enables telemetry and the selected data events through ioctl when it
	# configures the collector, so no separate enable operation is needed here.
}

main() {
	parse_args "$@"
	((EUID == 0)) || die "this script must be run as root"

	load_telemetry_driver
	wait_for_telemetry_devices
	select_interface
	case "${INTERFACE}" in
	sysfs) setup_sysfs_interface ;;
	ioctl) setup_ioctl_interface ;;
	esac

	echo "ARM AGI CPU telemetry boot setup complete (${INTERFACE} interface)."
	echo "ASTL will discover this interface automatically; ASTL_SCMI_INTERFACE is only needed to override it."
}

main "$@"
