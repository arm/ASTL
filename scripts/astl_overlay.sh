#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

readonly PROFILE="astl-combined"
readonly CONFIG_KEY="astl.overlayRoot"
REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)" || {
	echo "Failed to determine repository root" >&2
	exit 1
}
readonly REPO_ROOT

usage() {
	echo "Usage: $0 {enable SOURCE|refresh|check|disable}" >&2
	exit 2
}

require_git_checkout() {
	if ! git -C "${REPO_ROOT}" rev-parse --show-toplevel >/dev/null 2>&1; then
		echo "ASTL overlay management requires a Git checkout: ${REPO_ROOT}" >&2
		exit 2
	fi
}

overlay_root() {
	git -C "${REPO_ROOT}" config --local --get "${CONFIG_KEY}" 2>/dev/null || true
}

require_ossmosis() {
	local executable="${OSSMOSIS_BIN:-ossmosis}"
	if ! command -v "${executable}" >/dev/null 2>&1; then
		echo "The configured ASTL overlay requires ossmosis with materialize support." >&2
		echo "Install ossmosis or set OSSMOSIS_BIN to its executable path." >&2
		exit 2
	fi
	printf '%s\n' "${executable}"
}

materialize() {
	local operation="$1"
	local source="$2"
	local executable
	executable="$(require_ossmosis)"
	"${executable}" materialize "${operation}" \
		--source "${source}" \
		--target "${REPO_ROOT}" \
		--profile "${PROFILE}"
}

enable_overlay() {
	local requested_source="$1"
	local source
	if [[ ! -d ${requested_source} ]]; then
		echo "Overlay source is not a directory: ${requested_source}" >&2
		exit 2
	fi
	source="$(cd "${requested_source}" && pwd -P)"
	if [[ ! -f "${source}/.ossmosis.json" ]]; then
		echo "Overlay source has no .ossmosis.json: ${source}" >&2
		exit 2
	fi

	local current
	current="$(overlay_root)"
	if [[ -n ${current} && ${current} != "${source}" ]]; then
		local executable
		executable="$(require_ossmosis)"
		"${executable}" materialize clean \
			--target "${REPO_ROOT}" \
			--profile "${PROFILE}"
	fi

	git -C "${REPO_ROOT}" config --local "${CONFIG_KEY}" "${source}"
	materialize link "${source}"
	materialize check "${source}"
	echo "Enabled ASTL overlay: ${source}"
}

refresh_overlay() {
	local source
	source="$(overlay_root)"
	if [[ -z ${source} ]]; then
		exit 0
	fi
	materialize link "${source}"
	materialize check "${source}"
}

check_overlay() {
	local source
	source="$(overlay_root)"
	if [[ -z ${source} ]]; then
		echo "No ASTL overlay is configured. Run 'just overlay-enable PATH'." >&2
		exit 2
	fi
	materialize check "${source}"
}

disable_overlay() {
	local source
	source="$(overlay_root)"
	if [[ -z ${source} ]]; then
		echo "No ASTL overlay is configured."
		exit 0
	fi
	local executable
	executable="$(require_ossmosis)"
	"${executable}" materialize clean \
		--target "${REPO_ROOT}" \
		--profile "${PROFILE}"
	git -C "${REPO_ROOT}" config --local --unset-all "${CONFIG_KEY}" || true
	echo "Disabled ASTL overlay: ${source}"
}

command="${1:-}"
case "${command}" in
enable)
	[[ $# -eq 2 ]] || usage
	require_git_checkout
	enable_overlay "$2"
	;;
refresh)
	[[ $# -eq 1 ]] || usage
	if git -C "${REPO_ROOT}" rev-parse --show-toplevel >/dev/null 2>&1; then
		refresh_overlay
	fi
	;;
check)
	[[ $# -eq 1 ]] || usage
	require_git_checkout
	check_overlay
	;;
disable)
	[[ $# -eq 1 ]] || usage
	require_git_checkout
	disable_overlay
	;;
*)
	usage
	;;
esac
