#!/usr/bin/env bash

# This script auto-formats the code according to clang-format rules.
# run this from the repo root, or from CI
set -e

find . -path ./build -prune -o -regex '.*\.\(c\|cpp\|h\|hpp\|h.in\)' -exec clang-format -i {} +