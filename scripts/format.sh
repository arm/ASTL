#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# This script auto-formats the code according to clang-format rules.
# run this from the repo root, or from CI
set -eu -o pipefail

mode="all"
case "${1:-}" in
"") ;;
--staged) mode="staged" ;;
*)
	echo "Usage: $0 [--staged]" >&2
	exit 2
	;;
esac
if [[ $# -gt 1 ]]; then
	echo "Usage: $0 [--staged]" >&2
	exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

STAGED_FILES=()
if [[ $mode == "staged" ]]; then
	while IFS= read -r -d '' file; do
		STAGED_FILES+=("$file")
	done < <(git diff --cached --name-only --diff-filter=ACMR -z)
	if [[ ${#STAGED_FILES[@]} -eq 0 ]]; then
		echo "No staged files to format."
		exit 0
	fi
fi

# Check for clang-format
if ! command -v clang-format >/dev/null 2>&1; then
	echo "❌ clang-format is not installed."
	echo "👉 Please install it with:"
	echo "   sudo apt install clang-format        # Debian/Ubuntu"
	echo "   brew install clang-format            # macOS (Homebrew)"
	echo "   pacman -S clang                      # Arch"
	exit 1
fi

if ! command -v cmake-format >/dev/null 2>&1; then
	echo "❌ cmake-format is not installed."
	echo "👉 Please install the cmakelang Python package (not Homebrew's cmakelint):"
	echo "   brew install pipx && pipx install 'cmakelang[YAML]' # recommended on macOS"
	echo "   python3 -m pip install --user 'cmakelang[YAML]' # other Python setups"
	exit 1
fi

# use utils.sh's get_all_source_files to export SOURCE_FILES array
echo 'Running utils.sh'
# shellcheck disable=SC1091
source "${SCRIPT_DIR}"/utils.sh
get_all_source_files

# filter out  Cython generated files (_*.cpp files in python/astl/)
echo 'Generating list of source files to format'
SOURCE_FILES_TO_FORMAT=()
for file in "${SOURCE_FILES[@]}"; do
	if echo "$file" | grep --quiet 'python.*/astl/_.*\.cpp$'; then
		continue
	fi
	SOURCE_FILES_TO_FORMAT+=("$file")
done
echo 'Running clang-format'
clang-format -i "${SOURCE_FILES_TO_FORMAT[@]}"

echo 'Running cmake-format on CMakeLists.txt files'
find . \( -path './CMakeLists.txt' \
	-o -path './src/**/CMakeLists.txt' \
	-o -path './samples/**/CMakeLists.txt' \
	-o -path './tools/**/CMakeLists.txt' \
	-o -path './tests/**/CMakeLists.txt' \) -type f -print0 | xargs -0 cmake-format -i

# Format .sh files using qlty cli
if ! command -v qlty >/dev/null 2>&1; then
	echo "❌ qlty cli tool is not installed to auto-format .sh files."
	echo "👉 Please install it with the instructions here: https://docs.qlty.sh/cli/quickstart"
	qlty_available=false
else
	qlty_available=true
fi

# https://confluence.arm.com/display/ITINFRA/Using+npm+Mirrors
# set up an artifactory mirror for NPM packages, since direct access to npmjs.org is blocked on ARM network

PUBLIC_NPM="https://registry.npmjs.org/"

if [[ $qlty_available == true ]]; then
	echo 'Checking connectivity to public npm registry'
fi
probe() { # fast: 1s connect timeout, 2s overall, no output
	curl --silent --head --fail \
		--connect-timeout 1 --max-time 2 \
		--output /dev/null "$1"
}

# if we can't connect to the public npm register, use the artifactory mirror
if [[ $qlty_available == true ]] && ! probe "$PUBLIC_NPM"; then
	npm config set registry https://artifactory.arm.com/artifactory/api/npm/mirrors.npmjs_org
fi

if [[ $qlty_available == true ]]; then
	echo 'Running qlty'
	qlty fmt ./scripts/
	qlty fmt ./*.md
	qlty fmt ./.github/
fi

if [[ $mode == "staged" ]]; then
	echo 'Re-staging formatted files'
	for file in "${STAGED_FILES[@]}"; do
		if [[ -e "$REPO_ROOT/$file" ]]; then
			git add -- "$file"
		fi
	done
fi
