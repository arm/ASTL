#!/usr/bin/env bash

set -eu -o pipefail

# avoid unbound variable error even when -u is set
SOURCE_FILES=()

get_all_source_files() {
	local REPO_ROOT
	REPO_ROOT=$(git rev-parse --show-toplevel 2>/dev/null || realpath .)

	# Use find with -print0 for null-separated output (safe for spaces)
	# Then read into an array using mapfile or while-read loop
	local FILES=()

	if command -v find >/dev/null; then
		while IFS= read -r -d '' FILE; do
			FILES+=("$FILE")
		done < <(find "$REPO_ROOT" \( -path "$REPO_ROOT/build" -o -path "$REPO_ROOT/external/vcpkg" \) -prune -false -o \
			-type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.h.in' \) -print0)
	fi

	# Export array for caller to use
	SOURCE_FILES=("${FILES[@]}")
	export SOURCE_FILES
}
