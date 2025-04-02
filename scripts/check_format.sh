#!/usr/bin/env bash

# This script checks if the code is formatted according to clang-format rules.
# run this from the repo root
set -e

FILES=$(find . -path ./build -prune -o -regex '.*\.\(c\|cpp\|h\|hpp\|h.in\)' -print)

if ! clang-format --dry-run --Werror $FILES; then
  echo "💥 Code is not properly formatted! Run 'cmake --build . --target format'"
  exit 1
fi