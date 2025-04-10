#!/usr/bin/env bash

# This script checks if the code is formatted according to clang-format rules.
# run this from the repo root
set -e

# Check for clang-format
if ! command -v clang-format >/dev/null 2>&1; then
    echo "❌ clang-format is not installed."
    echo "👉 Please install it with:"
    echo "   sudo apt install clang-format        # Debian/Ubuntu"
    echo "   brew install clang-format            # macOS (Homebrew)"
    echo "   pacman -S clang                      # Arch"
    exit 1
fi

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $SCRIPT_DIR/get_find_file_expressions.sh  # define PRUNE_EXPR

FILES=$(find $(realpath .) \( $PRUNE_EXPR \) -prune -o \( -type f \( $NAME_ALL_SOURCES_AND_HEADERS  \) \) -print)


if ! clang-format --dry-run --Werror $FILES; then
  echo "💥 Code is not properly formatted! Run 'cmake --build . --target format'"
  exit 1
fi

echo "✅ Code is properly formatted!"
