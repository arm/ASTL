#!/usr/bin/env bash

# This script checks if the code is formatted according to clang-format rules.
# run this from the repo root
set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $SCRIPT_DIR/get_find_file_expressions.sh  # define PRUNE_EXPR

FILES=$(find $(realpath .) \( $PRUNE_EXPR \) -prune -o \( -type f \( $NAME_ALL_SOURCES_AND_HEADERS  \) \) -print)

if ! clang-format --dry-run --Werror $FILES; then
  echo "💥 Code is not properly formatted! Run 'cmake --build . --target format'"
  exit 1
fi

echo "✅ Code is properly formatted!"