#!/usr/bin/env bash

# This script generates 3 variables for find to use to make lint/format scripts easy to use.
# PRUNE_EXPR - a path expression to use within parens before a -prune expression, including directories we don't want to lint/format
# NAME_ALL_SOURCES_AND_HEADERS - a name expression to use with find to locate all source files (including headers)
# NAME_ALL_SOURCES - a name expression to use with find to locate all source files (excluding headers)

set -eu -o pipefail

EXCLUDE_DIRS=("build" "vcpkg")
PRUNE_EXPR=" -path build -o"
for dir in "${EXCLUDE_DIRS[@]}"; do
   if [ -d $dir ] && realpath $dir>/dev/null 2>&1; then
       PRUNE_EXPR+=" -path $(realpath $dir) -o"
   fi
done

# remove the trailing -o
if [ ${#PRUNE_EXPR} -ge 2 ]; then
    PRUNE_EXPR="${PRUNE_EXPR:0:${#PRUNE_EXPR}-2}"
fi

# create NAME_ALL_SOURCES_AND_HEADERS expression
ALL_SOURCE_EXTENSIONS=("cpp" "c" "h" "hpp" "h.in")
NAME_ALL_SOURCES_AND_HEADERS=""
for ext in "${ALL_SOURCE_EXTENSIONS[@]}"; do
  NAME_ALL_SOURCES_AND_HEADERS+=" -name *.$ext -o"
done
# remove the trailing -o
if [ ${#NAME_ALL_SOURCES_AND_HEADERS} -ge 2 ]; then
    NAME_ALL_SOURCES_AND_HEADERS="${NAME_ALL_SOURCES_AND_HEADERS:0:${#NAME_ALL_SOURCES_AND_HEADERS}-2}"
fi

# create NAME_ALL_SOURCES expression
SOURCE_EXTENSIONS=("cpp" "c")
NAME_ALL_SOURCES=""
for ext in "${SOURCE_EXTENSIONS[@]}"; do
  NAME_ALL_SOURCES+=" -name *.$ext -o"
done
# remove the trailing -o
if [ ${#NAME_ALL_SOURCES} -ge 2 ]; then
    NAME_ALL_SOURCES="${NAME_ALL_SOURCES:0:${#NAME_ALL_SOURCES}-2}"
fi
