#!/usr/bin/env bash

# This script auto-formats the code according to clang-format rules.
# run this from the repo root, or from CI
set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $SCRIPT_DIR/get_find_file_expressions.sh  # define PRUNE_EXPR

FILES=$(find $(realpath .) \( $PRUNE_EXPR \) -prune -o \( -type f \( $NAME_ALL_SOURCES_AND_HEADERS  \) \) -print)
clang-format -i $FILES
 