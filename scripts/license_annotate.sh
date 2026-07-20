#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# This script uses the `reuse` tool https://reuse.software/ to check that all source files have a valid license header.
# run this from the repo root, or from CI
set -eu -o pipefail

# Check for `reuse`
if ! command -v reuse >/dev/null 2>&1; then
	echo "❌ reuse is not installed."
	echo "👉 Please install it with:"
	echo "   sudo apt install reuse           # Debian/Ubuntu"
	echo "   brew install reuse               # macOS (Homebrew)"
	echo "   pacman -S reuse                  # Arch"
	exit 1
fi

#if ! reuse --help 2>&1 | grep -q "REUSE.toml"; then
#	echo "❌ reuse version 3.3 is required. Please upgrade to version 3.3 or later."
#	echo "👉 Please install it, maybe with pip!"
#	exit 1
#fi

# Check for `jq`
if ! command -v jq >/dev/null 2>&1; then
	echo "❌ jq is not installed."
	echo "👉 Please install it with:"
	echo "   sudo apt install jq           # Debian/Ubuntu"
	echo "   brew install jq               # macOS (Homebrew)"
	echo "   pacman -S jq                  # Arch"
	exit 1
fi

annotate() {
	local path="$1"
	local style="${2:-}"
	local -a annotate_args
	annotate_args=(
		--year="$(date +%Y)"
		--copyright="Arm Limited and/or its affiliates <open-source-office@arm.com>"
		--copyright-prefix spdx-string
		--license="Apache-2.0"
		--merge-copyrights
		--recursive "$path"
	)

	if [[ -n $style ]]; then
		annotate_args+=(--style "$style")
	fi

	reuse annotate "${annotate_args[@]}"
}

style=""
paths=()

while [[ $# -gt 0 ]]; do
	case "$1" in
	--style)
		if [[ $# -lt 2 ]]; then
			echo "Error: --style requires a value"
			echo "Usage: $0 [path ...] [--style <style>]"
			exit 1
		fi
		style="$2"
		shift 2
		;;
	--style=*)
		style="${1#*=}"
		shift
		;;
	--help | -h)
		echo "Usage: $0 [path ...] [--style <style>]"
		exit 0
		;;
	-*)
		echo "Error: unknown option '$1'"
		echo "Usage: $0 [path ...] [--style <style>]"
		exit 1
		;;
	*)
		paths+=("$1")
		shift
		;;
	esac
done

if [[ ${#paths[@]} -gt 0 ]]; then
	for path in "${paths[@]}"; do
		annotate "$path" "$style"
	done
else
	# No file path specified, so run the lint command, output json, and query it into a list of non-compliant files.
	# then annotate each of those files.
	reuse lint --json |
		jq -r '.non_compliant.missing_licensing_info + .non_compliant.missing_copyright_info | unique | .[]' |
		while IFS= read -r item; do
			annotate "$item" "$style"
		done
fi
