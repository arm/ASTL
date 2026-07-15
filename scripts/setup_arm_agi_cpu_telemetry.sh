#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

TELEMETRY_ROOT="/sys/fs/arm_telemetry"

########################################
# Mount stlmfs if not already mounted  #
########################################
if [[ ! -d ${TELEMETRY_ROOT} ]]; then
	echo "Mounting stlmfs at ${TELEMETRY_ROOT}..."
	mkdir -p "${TELEMETRY_ROOT}"
	mount -t stlmfs none "${TELEMETRY_ROOT}"
else
	echo "${TELEMETRY_ROOT} already exists."
	# Check if telemetry devices are present; if not, remount
	if [[ ! -d ${TELEMETRY_ROOT}/tlm_0 ]] && [[ ! -d ${TELEMETRY_ROOT}/tlm_1 ]]; then
		echo "Telemetry devices not found. Remounting ${TELEMETRY_ROOT}..."
		umount "${TELEMETRY_ROOT}" || true
		mount -t stlmfs none "${TELEMETRY_ROOT}"
	fi
fi

########################################
# Set permissions                      #
########################################
chmod -R 777 "${TELEMETRY_ROOT}" || echo "Warning: Could not change permissions on ${TELEMETRY_ROOT}" >&2

########################################
# Enable telemetry on tlm_0 and tlm_1  #
########################################
for tlm in tlm_0 tlm_1; do
	TLM_DIR="${TELEMETRY_ROOT}/${tlm}"
	if [[ -d ${TLM_DIR} ]]; then
		echo "Enabling telemetry on ${tlm}..."
		echo y >"${TLM_DIR}/tlm_enable"
		echo y >"${TLM_DIR}/all_des_enable"
	else
		echo "Warning: ${TLM_DIR} not found, skipping ${tlm}." >&2
	fi
done

echo "ARM AGI CPU telemetry boot setup complete."
