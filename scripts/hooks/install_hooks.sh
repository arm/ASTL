#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# Install the appropriate pre-commit configuration for this checkout.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR/../.." rev-parse --show-toplevel 2>/dev/null)" || {
	echo "Error: ASTL hook installation requires a Git checkout." >&2
	exit 2
}

usage() {
	echo "Usage: $0 [--internal|--public]" >&2
	exit 2
}

normalize_github_repo() {
	local url="${1%.git}"
	url="${url%/}"
	case "$url" in
	git@github.com:*) printf 'github.com/%s\n' "${url#git@github.com:}" ;;
	ssh://git@github.com/*) printf 'github.com/%s\n' "${url#ssh://git@github.com/}" ;;
	https://github.com/*) printf 'github.com/%s\n' "${url#https://github.com/}" ;;
	http://github.com/*) printf 'github.com/%s\n' "${url#http://github.com/}" ;;
	*) printf '%s\n' "$url" ;;
	esac
}

mode="auto"
case "${1:-}" in
"") ;;
--internal) mode="internal" ;;
--public) mode="public" ;;
*) usage ;;
esac
[[ $# -le 1 ]] || usage

if ! command -v pre-commit >/dev/null 2>&1; then
	echo "Error: pre-commit is required. Install it from https://pre-commit.com/." >&2
	exit 2
fi

origin_url="$(git -C "$REPO_ROOT" config --get remote.origin.url 2>/dev/null || true)"
normalized_origin="$(normalize_github_repo "$origin_url")"
if [[ $mode == "auto" ]]; then
	if [[ $normalized_origin == "github.com/Arm-Debug/ASTL" ]]; then
		mode="internal"
	else
		mode="public"
	fi
fi

if [[ $mode == "internal" ]]; then
	config=".pre-commit-config-arm-debug.yaml"
else
	config=".pre-commit-config.yaml"
fi

if [[ ! -f "$REPO_ROOT/$config" ]]; then
	echo "Error: selected pre-commit configuration is missing: $config" >&2
	exit 2
fi

echo "Installing $mode ASTL hooks from $config"
(
	cd "$REPO_ROOT"
	pre-commit install --config "$config" --install-hooks --overwrite
)
echo "Installed ASTL pre-commit and commit-msg hooks."
