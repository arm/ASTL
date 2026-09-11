#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)" || {
	echo "Failed to determine repository root" >&2
	exit 1
}
readonly REPO_ROOT
readonly OSSMOSIS_REF="8b005c7db5a4d349abb9a8a1693a2e7bb36e9421"
readonly OSSMOSIS_PACKAGE="ossmosis @ git+https://github.com/Arm-Debug/ossmosis.git@${OSSMOSIS_REF}"
readonly OSSMOSIS_VENV="${REPO_ROOT}/.venv-ossmosis"

supports_materialize() {
	local executable="$1"
	command -v "${executable}" >/dev/null 2>&1 && "${executable}" materialize --help >/dev/null 2>&1
}

if [[ -n ${OSSMOSIS_BIN:-} ]]; then
	if ! supports_materialize "${OSSMOSIS_BIN}"; then
		echo "OSSMOSIS_BIN does not provide ossmosis materialize: ${OSSMOSIS_BIN}" >&2
		exit 2
	fi
	printf '%s\n' "${OSSMOSIS_BIN}"
	exit 0
fi

if supports_materialize ossmosis; then
	printf '%s\n' ossmosis
	exit 0
fi

managed_executable="${OSSMOSIS_VENV}/bin/ossmosis"
if supports_materialize "${managed_executable}"; then
	printf '%s\n' "${managed_executable}"
	exit 0
fi

python_bin="${PYTHON:-python3}"
if ! command -v "${python_bin}" >/dev/null 2>&1; then
	echo "Installing ossmosis requires Python 3.10 or newer and Git." >&2
	echo "Install Python, or set PYTHON to a Python 3.10+ executable." >&2
	exit 2
fi

echo "[overlay] Installing ossmosis into ${OSSMOSIS_VENV}" >&2
"${python_bin}" -m venv "${OSSMOSIS_VENV}"
"${OSSMOSIS_VENV}/bin/python" -m pip install "${OSSMOSIS_PACKAGE}" >&2

if ! supports_materialize "${managed_executable}"; then
	echo "The installed ossmosis does not provide materialize support." >&2
	exit 2
fi

printf '%s\n' "${managed_executable}"
