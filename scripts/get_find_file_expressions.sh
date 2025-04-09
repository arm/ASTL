#!/usr/bin/env bash

# This script generates 3 variables for find to use to make lint/format scripts easy to use.
# PRUNE_EXPR - a path expression to use within parens before a -prune expression, including directories we wdon't wnat to lint/format
# NAME_ALL_SOURCES_AND_HEADERS - a name expression to use with find to local all source files (including headers)
# NAME_ALL_SOURCES - a name expression to use with find to local all source files (excluding headers)

EXCLUDE_DIRS=("build" "vcpkg")
PRUNE_EXPR=""
for dir in "${EXCLUDE_DIRS[@]}"; do
  PRUNE_EXPR+=" -path $(realpath $dir) -o"
done

# remove the trailing -o
PRUNE_EXPR=${PRUNE_EXPR::-2}  

# create NAME_ALL_SOURCES_AND_HEADERS expression
ALL_SOURCE_EXTENSIONS=("cpp" "c" "h" "hpp" "h.in")
NAME_ALL_SOURCES_AND_HEADERS=""
for ext in "${ALL_SOURCE_EXTENSIONS[@]}"; do
  NAME_ALL_SOURCES_AND_HEADERS+=" -name *.$ext -o"
done
# remove the trailing -o
NAME_ALL_SOURCES_AND_HEADERS=${NAME_ALL_SOURCES_AND_HEADERS::-2}

# create NAME_ALL_SOURCES expression
SOURCE_EXTENSIONS=("cpp" "c")
NAME_ALL_SOURCES=""
for ext in "${SOURCE_EXTENSIONS[@]}"; do
  NAME_ALL_SOURCES+=" -name *.$ext -o"
done
# remove the trailing -o
NAME_ALL_SOURCES=${NAME_ALL_SOURCES::-2}




