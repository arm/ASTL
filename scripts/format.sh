#!/usr/bin/env bash

# This script auto-formats the code according to clang-format rules.
# run this from the repo root, or from CI
set -eu -o pipefail

# Check for clang-format
if ! command -v clang-format >/dev/null 2>&1; then
	echo "❌ clang-format is not installed."
	echo "👉 Please install it with:"
	echo "   sudo apt install clang-format        # Debian/Ubuntu"
	echo "   brew install clang-format            # macOS (Homebrew)"
	echo "   pacman -S clang                      # Arch"
	exit 1
fi

# use utils.sh's get_all_source_files to export SOURCE_FILES array
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}"/utils.sh
get_all_source_files

# filter out  Cython generated files (_*.cpp files in python/astl/)
SOURCE_FILES_TO_FORMAT=()
for file in "${SOURCE_FILES[@]}"; do
	if [[ $file == python/astl/_*.cpp ]]; then
		continue
	fi
	SOURCE_FILES_TO_FORMAT+=("$file")
done
clang-format -i "${SOURCE_FILES_TO_FORMAT[@]}"

# Format .sh files using qlty cli
if ! command -v qlty >/dev/null 2>&1; then
	echo "❌ qlty cli tool is not installed to auto-format .sh files."
	echo "👉 Please install it with the instructions here: https://docs.qlty.sh/cli/quickstart"
	exit 0
fi

qlty fmt ./scripts/
qlty fmt ./*.md
qlty fmt ./.github/
