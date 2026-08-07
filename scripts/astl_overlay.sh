#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

readonly DEFAULT_PROFILE="astl-combined"
readonly ROOT_CONFIG_KEY="astl.overlayRoot"
readonly PROFILE_CONFIG_KEY="astl.overlayProfile"
REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)" || {
	echo "Failed to determine repository root" >&2
	exit 1
}
readonly REPO_ROOT

usage() {
	echo "Usage: $0 {enable SOURCE [PROFILE]|refresh|check|disable}" >&2
	exit 2
}

require_git_checkout() {
	if ! git -C "${REPO_ROOT}" rev-parse --show-toplevel >/dev/null 2>&1; then
		echo "ASTL overlay management requires a Git checkout: ${REPO_ROOT}" >&2
		exit 2
	fi
}

overlay_root() {
	git -C "${REPO_ROOT}" config --local --get "${ROOT_CONFIG_KEY}" 2>/dev/null || true
}

overlay_profile() {
	git -C "${REPO_ROOT}" config --local --get "${PROFILE_CONFIG_KEY}" 2>/dev/null || printf '%s\n' "${DEFAULT_PROFILE}"
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
	local profile="$3"
	local executable
	executable="$(require_ossmosis)"
	"${executable}" materialize "${operation}" \
		--source "${source}" \
		--target "${REPO_ROOT}" \
		--profile "${profile}"
}

enable_overlay() {
	local requested_source="$1"
	local requested_profile="$2"
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

	local current current_profile
	current="$(overlay_root)"
	current_profile="$(overlay_profile)"
	if [[ -n ${current} && (${current} != "${source}" || ${current_profile} != "${requested_profile}") ]]; then
		local executable
		executable="$(require_ossmosis)"
		"${executable}" materialize clean \
			--target "${REPO_ROOT}" \
			--profile "${current_profile}"
	fi

	git -C "${REPO_ROOT}" config --local "${ROOT_CONFIG_KEY}" "${source}"
	git -C "${REPO_ROOT}" config --local "${PROFILE_CONFIG_KEY}" "${requested_profile}"
	if ! materialize link "${source}" "${requested_profile}" || ! materialize check "${source}" "${requested_profile}"; then
		local executable
		executable="$(require_ossmosis)"
		"${executable}" materialize clean --target "${REPO_ROOT}" --profile "${requested_profile}" || true
		git -C "${REPO_ROOT}" config --local --unset-all "${ROOT_CONFIG_KEY}" || true
		git -C "${REPO_ROOT}" config --local --unset-all "${PROFILE_CONFIG_KEY}" || true
		if [[ -n ${current} ]]; then
			git -C "${REPO_ROOT}" config --local "${ROOT_CONFIG_KEY}" "${current}"
			git -C "${REPO_ROOT}" config --local "${PROFILE_CONFIG_KEY}" "${current_profile}"
			materialize link "${current}" "${current_profile}" || true
			materialize check "${current}" "${current_profile}" || true
		fi
		return 1
	fi
	echo "Enabled ASTL overlay: ${source} (profile: ${requested_profile})"
}

refresh_overlay() {
	local source
	source="$(overlay_root)"
	if [[ -z ${source} ]]; then
		exit 0
	fi
	local profile
	profile="$(overlay_profile)"
	materialize link "${source}" "${profile}"
	materialize check "${source}" "${profile}"
}

check_overlay() {
	local source
	source="$(overlay_root)"
	if [[ -z ${source} ]]; then
		echo "No ASTL overlay is configured. Run 'just overlay-enable PATH'." >&2
		exit 2
	fi
	local profile
	profile="$(overlay_profile)"
	materialize check "${source}" "${profile}"
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
	local profile
	profile="$(overlay_profile)"
	"${executable}" materialize clean \
		--target "${REPO_ROOT}" \
		--profile "${profile}"
	git -C "${REPO_ROOT}" config --local --unset-all "${ROOT_CONFIG_KEY}" || true
	git -C "${REPO_ROOT}" config --local --unset-all "${PROFILE_CONFIG_KEY}" || true
	echo "Disabled ASTL overlay: ${source} (profile: ${profile})"
}

command="${1:-}"
case "${command}" in
enable)
	[[ $# -eq 2 || $# -eq 3 ]] || usage
	require_git_checkout
	enable_overlay "$2" "${3:-${DEFAULT_PROFILE}}"
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
