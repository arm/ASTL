#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -eu -o pipefail

# avoid unbound variable error even when -u is set
SOURCE_FILES=()

get_all_source_files() {
	local REPO_ROOT
	REPO_ROOT=$(git rev-parse --show-toplevel 2>/dev/null || realpath .)
	local -a PRUNED_DIRS=(
		"$REPO_ROOT/.git"
		"$REPO_ROOT/.mypy_cache"
		"$REPO_ROOT/.pytest_cache"
		"$REPO_ROOT/.qlty"
		"$REPO_ROOT/.venv"
		"$REPO_ROOT/.vscode"
		"$REPO_ROOT/Testing"
		"$REPO_ROOT/artifacts"
		"$REPO_ROOT/build"
		"$REPO_ROOT/external/vcpkg"
		"$REPO_ROOT/python/.pytest_cache"
		"$REPO_ROOT/python/astl.egg-info"
		"$REPO_ROOT/python/build"
		"$REPO_ROOT/python/dist"
	)
	local -a PRUNED_FILES=(
		"$REPO_ROOT/python/astl/_core.cpp"
	)

	# Use find with -print0 for null-separated output (safe for spaces)
	# Then read into an array using mapfile or while-read loop
	local FILES=()

	if command -v find >/dev/null; then
		local -a FIND_PRUNE_EXPR=("(")
		local -a FIND_SKIP_FILE_EXPR=()
		local prune_dir
		for prune_dir in "${PRUNED_DIRS[@]}"; do
			FIND_PRUNE_EXPR+=(-path "$prune_dir" -o)
		done
		unset "FIND_PRUNE_EXPR[${#FIND_PRUNE_EXPR[@]}-1]"
		FIND_PRUNE_EXPR+=(")" -prune -false -o)

		local skip_file
		for skip_file in "${PRUNED_FILES[@]}"; do
			FIND_SKIP_FILE_EXPR+=(-not -path "$skip_file")
		done

		while IFS= read -r -d '' FILE; do
			FILES+=("$FILE")
		done < <(find "$REPO_ROOT" "${FIND_PRUNE_EXPR[@]}" \
			-type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.h.in' \) \
			"${FIND_SKIP_FILE_EXPR[@]}" -print0)
	fi

	# Export array for caller to use
	SOURCE_FILES=("${FILES[@]}")
	export SOURCE_FILES
}
