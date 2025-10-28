#!/usr/bin/env bash

# This script checks if the code is formatted according to clang-format rules.
# run this from the repo root
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

# use utils.sh's get_all_source_files to export  SOURCE_FILES
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/utils.sh"
get_all_source_files

# filter out  Cython generated files (_*.cpp files in python/astl/)
SOURCE_FILES_TO_CHECK=()
for file in "${SOURCE_FILES[@]}"; do
	if [[ $file == python/astl/_*.cpp ]]; then
		continue
	fi
	SOURCE_FILES_TO_CHECK+=("$file")
done

if ! clang-format --dry-run --Werror "${SOURCE_FILES_TO_CHECK[@]}"; then
	echo "💥 Code is not properly formatted! Run 'cmake --build . --target format'"
	exit 1
fi

echo "✅ Code is properly formatted!"
