#!/usr/bin/env bash

# This script finds source files and runs clang-tidy to lint them
set -eu -o pipefail

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 <build> [pre-commit|all|pull-request]"
    echo "build path is needed to help clang-tidy avoid linting cmake-generated source files."
    exit 1
fi

# Check for clang-tidy
if ! command -v clang-tidy >/dev/null 2>&1; then
    echo "❌ clang-tidy is not installed."
    echo "👉 Please install it with:"
    echo "   sudo apt install clang-tidy        # Debian/Ubuntu"
    echo "   brew install llvm                                # macOS (Homebrew)"
    echo "   export PATH="/opt/homebrew/opt/llvm/bin:\$PATH"  # macOS continued"
    echo "   pacman -S clang                    # Arch"
    exit 1
fi

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT_DIR="$( dirname ${SCRIPT_DIR} )"
INCLUDE_PATHS="-I${REPO_ROOT_DIR}/include"

# don't lint builld dir, or vcpkg dependencies
source $SCRIPT_DIR/get_find_file_expressions.sh # define PRUNE_EXPR

CLANG_BUILD_DIR=""
# if the script was given an argument for the build output, add the include path from that, and exclude it from linting
if [[ -n "$1" ]]; then
  BUILD_DIR=$(realpath "$1")

  INCLUDE_PATHS+=" -I${BUILD_DIR}/include"
  INCLUDE_PATHS+=" -I${REPO_ROOT_DIR}/utils"
  INCLUDE_PATHS+=" -I${REPO_ROOT_DIR}/src/impl"
  INCLUDE_PATHS+=" -I${REPO_ROOT_DIR}/src/impl/common"
  INCLUDE_PATHS+=" -I${REPO_ROOT_DIR}/tools/mock_sysfs/include"
  CLANG_BUILD_DIR+=" -p $BUILD_DIR"
fi

echo "Running clang-tidy to lint code..."

# find the system header paths so clang++ can find them
SYS_INCLUDE_PATHS=$(echo | g++ -E -x c++ - -v 2>&1 | \
  awk '/#include <...> search starts here:/{flag=1;next}/End of search list/{flag=0}flag' | \
  sed 's/^/ -isystem /')

# Include dependency headers from vcpkg as system headers
for DEP in $REPO_ROOT_DIR/vcpkg/packages/*; do
  NEW_INCLUDE="${DEP}/include/"
  if [[ -d "${NEW_INCLUDE}/fuse3" ]]; then
    # vcpkg installs fuse3 headers in a subdirectory
    NEW_INCLUDE="${NEW_INCLUDE}/fuse3"
  fi
  SYS_INCLUDE_PATHS+=" -isystem ${NEW_INCLUDE}"
done

# helpers
get_staged_files() {
  git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|cc|cxx|c|h|hpp|h|hxx)$' || true
}

get_diff_files() {
  # .github/workflows/integration.yml should set up the env variables for BASE_REF and HEAD_REF,
  # but provide reasonable default
  BASE_REF="${BASE_REF:-main}"
  CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
  HEAD_REF="${HEAD_REF:-${CURRENT_BRANCH}}"
  git fetch origin "$BASE_REF" "$HEAD_REF"
  {
    # note: don't lint .h files, as the -std=c++23 flag cannot be applied to them. we'll treat them in a separate step
    git diff origin/"$BASE_REF"...origin/"$HEAD_REF" --name-only --diff-filter=ACM | grep -E '\.(cpp|cc|cxx|c|hpp|hxx)$' || true
    git diff --cached           --name-only --diff-filter=ACM | grep -E '\.(cpp|cc|cxx|c|hpp|hxx)$' || true
  } | sort -u
}

if [ "$#" -lt 2 ]; then
  MODE="pull-request"
else
  MODE=$2
fi

echo "Linting mode: $MODE"

case "$MODE" in
  pre-commit)
    FILES=$(get_staged_files)
    ;;
  pull-request)
    FILES=$(get_diff_files)
    ;;
  all)
    # lint one translation unit at a time (assume headers are #included)
    FILES=$(find $REPO_ROOT_DIR \( $PRUNE_EXPR \) -prune -o \( -type f \( $NAME_ALL_SOURCES_AND_HEADERS \) \) -print)
    ;;
  *)
    echo "Unknown diff mode $2. Use 'all' or pre-commit"
    exit 1
    ;;
esac

FILES_TO_LINT=()
case "$(uname -s)" in
  Linux)
    # on linux, we want to lint everything
    FILES_TO_LINT=("${FILES}")
    ;;
  *)
    # on macOS(Darwin) or other unknown, we want to lint everything except mock_sysfs
    for FILE in ${FILES[@]}; do
      if [[ "$FILE" == "" ]]; then
        continue
      elif [[ "$FILE" == *tools/mock_sysfs* ]]; then
        echo "Skipping linting of $FILE since it's not build on this platform"
      else
        FILES_TO_LINT+=("$FILE")
      fi
    done
    ;;
esac

for FILE in "${FILES_TO_LINT[@]-}"; do
  if [[ "$FILE" == "" ]]; then
    # skip empty string in case of empty file list
    continue
  fi
  echo "- Linting '$FILE'"
  # exclude the C-level headers from this C++ lint - we'll do them as a separate step
  header_filter="-header-filter='^(?!.*(include/astl|$BUILD_DIR/include/astl)).*'"
  EXTRA_ARGS=""
  if [[ "$FILE" != *.h ]]; then
    EXTRA_ARGS+=" --extra-arg=-std=c++23"
  fi
  # enable std::expected in clang-tidy
  EXTRA_ARGS+=" --extra-arg=-D__cpp_concepts=202002L"

  # set the version of the FUSE library. this should match the FUSE_USE_VERSION defined in tools/mock_sysfs/CMakeLists.txt
  EXTRA_ARGS+=" --extra-arg=-DFUSE_USE_VERSION=316"

  clang-tidy $header_filter \
    $FILE $CLANG_BUILD_DIR \
    $EXTRA_ARGS \
    --warnings-as-errors=* \
    -- \
    $INCLUDE_PATHS \
    $SYS_INCLUDE_PATHS

  # enable this to fix certain checks:
  # clang-tidy $CLANG_BUILD_DIR -checks=-*,readability-identifier-naming -fix -fix-errors -header-filter=. $FILE -- $INCLUDE_PATHS $SYS_INCLUDE_PATHS
done

# create a temporary C file to include all the C headers. This will cause clang-tidy to evaluate them as C headers
# (We had excluded them before to keep clang-tidy from trying to lint them as C++ headers)
echo "- Linting C Headers"
SRC_CODE='#include <astl/astl.h>\nint main() { return 0; }'
TEMP_C=$(mktemp "${TMPDIR:-/tmp}/tmp.XXXXXX.c")

trap "rm -f '$TEMP_C'" EXIT
echo -e $SRC_CODE > $TEMP_C
# lint the dummy C file that includes ASTL API files
clang-tidy -header-filter='^(.*(include|$BUILD_DIR/include)).*' $TEMP_C $CLANG_BUILD_DIR --warnings-as-errors=* -- $INCLUDE_PATHS $SYS_INCLUDE_PATHS
echo "✅ Linting completed successfully."
