#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# This script uses the `reuse` tool https://reuse.software/ to check that all source files have a valid license header.
# run this from the repo root, or from CI
set -eu -o pipefail

if ! command -v reuse >/dev/null; then
	echo "❌ reuse is not installed."
	echo "👉 Please install it with:"
	echo "   sudo apt install reuse           # Debian/Ubuntu"
	echo "   brew install reuse               # macOS (Homebrew)"
	echo "   pacman -S reuse                  # Arch"
	exit 1
fi

if ! command -v jq >/dev/null; then
	echo "❌ jq is not installed."
	echo "👉 Please install it with your package manager."
	exit 1
fi

if ! reuse --help 2>&1 | grep -q "REUSE.toml"; then
	echo "❌ reuse version 3.3 is required. Please upgrade to version 3.3 or later."
	echo "👉 Please install it, maybe with pip!"
	exit 2
fi

set +e
REUSE_LINT_JSON=$(reuse lint -j)
REUSE_LINT_STATUS=$?
set -e

if ! jq -e . >/dev/null <<<"$REUSE_LINT_JSON"; then
	echo "❌ reuse lint did not return valid JSON."
	if [[ $REUSE_LINT_STATUS -ne 0 ]]; then
		exit "$REUSE_LINT_STATUS"
	fi
	exit 2
fi

# if more than 0 files have more than 2 copyrights, fail the script
FILES_WITH_TOO_MANY_COPYRIGHTS=$(jq -r '.files[] | select((.copyrights // []) | length > 2) | .path' <<<"$REUSE_LINT_JSON" |
	grep -v tinyexpr || true)

NON_COMPLIANT_FILES=$(jq -r '
	(.non_compliant.missing_licensing_info // []) +
	(.non_compliant.missing_copyright_info // []) |
	unique |
	.[]
' <<<"$REUSE_LINT_JSON")

#echo "$FILES_WITH_TOO_MANY_COPYRIGHTS"
#echo "number of FILES: $(echo "$FILES_WITH_TOO_MANY_COPYRIGHTS" | wc -l)"
# if there are files with more than 2 copyrights, print the list and exit with error
if [[ -n $FILES_WITH_TOO_MANY_COPYRIGHTS ]]; then
	echo "❌ Some files have more than 2 copyrights. Please double check the below list and fix."
	echo "$FILES_WITH_TOO_MANY_COPYRIGHTS"
	exit 3
fi

if [[ -n $NON_COMPLIANT_FILES ]]; then
	echo "❌ Some files are missing copyright or license information. Please fix the below list."
	echo "$NON_COMPLIANT_FILES"
	exit 4
fi

echo "Congratulations! All files have a valid license and copyright identifier according to REUSE guidelines."
