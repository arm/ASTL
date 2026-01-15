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
	echo "   export PATH=\"/opt/homebrew/opt/llvm/bin:$PATH\" # macOS continued"
	echo "   pacman -S clang                    # Arch"
	exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT_DIR="$(dirname "${SCRIPT_DIR}")"

# use utils.sh's get_all_source_files to export  SOURCE_FILES
# shellcheck disable=SC1091
source "$SCRIPT_DIR"/utils.sh
get_all_source_files # export SOURCE_FILES
BUILD_DIR=$(realpath "$1")

INCLUDE_PATHS=(-I"${REPO_ROOT_DIR}"/include)
INCLUDE_PATHS+=(-I"${BUILD_DIR}"/include)
INCLUDE_PATHS+=(-I"${REPO_ROOT_DIR}"/utils)
INCLUDE_PATHS+=(-I"${REPO_ROOT_DIR}"/src/impl)
INCLUDE_PATHS+=(-I"${REPO_ROOT_DIR}"/src/impl/common)
INCLUDE_PATHS+=(-I"${REPO_ROOT_DIR}"/tools/mock_sysfs/include)
INCLUDE_PATHS+=(-I"${REPO_ROOT_DIR}"/third_party/tinyexpr-plusplus)

echo "Running clang-tidy to lint code..."
# find the system header paths so clang++ can find them
GCC_INCLUDE_PATHS=$(echo | g++ -E -x c++ - -v 2>&1 |
	awk '/#include <...> search starts here:/{flag=1;next}/End of search list/{flag=0}flag' |
	sed -E 's/^\s+//')
SYS_INCLUDE_PATHS=()
while IFS= read -r SYSTEM_INCLUDE; do
	SYS_INCLUDE_PATHS+=(-isystem)
	SYS_INCLUDE_PATHS+=("${SYSTEM_INCLUDE}")
done <<<"$GCC_INCLUDE_PATHS"

# Include dependency headers from vcpkg as system headers
for DEP in "$REPO_ROOT_DIR"/external/vcpkg/packages/*; do
	NEW_INCLUDE="${DEP}/include/"
	if [[ -d "${NEW_INCLUDE}/fuse3" ]]; then
		# vcpkg installs fuse3 headers in a subdirectory
		NEW_INCLUDE="${NEW_INCLUDE}/fuse3"
	fi
	SYS_INCLUDE_PATHS+=(-isystem "${NEW_INCLUDE}")
done

# helpers
get_staged_files() {
	git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|cc|cxx|c|h|hpp|hxx)$' || true
}

get_diff_files() {
	# .github/workflows/integration.yml should set up the env variables for BASE_REF and HEAD_REF,
	# but provide reasonable default
	BASE_REF="${BASE_REF:-main}"
	CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
	HEAD_REF="${HEAD_REF:-${CURRENT_BRANCH}}"
	git fetch origin "$BASE_REF" "$HEAD_REF"
	{
		git diff origin/"$BASE_REF"...origin/"$HEAD_REF" --name-only --diff-filter=ACM | grep -E '\.(cpp|cc|cxx|c|h|hpp|hxx)$' || true
		git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|cc|cxx|c|h|hpp|hxx)$' || true
	} | sort -u
}

if [ "$#" -lt 2 ]; then
	MODE="pull-request"
else
	MODE=$2
fi

echo "Linting mode: $MODE"

FILES=()
case "$MODE" in
pre-commit)
	while IFS= read -r LINE; do
		FILES+=("$LINE")
	done < <(get_staged_files)
	;;
pull-request)
	while IFS= read -r LINE; do
		FILES+=("$LINE")
	done < <(get_diff_files)
	;;
all)
	# lint one translation unit at a time (assume headers are #included)
	FILES=("${SOURCE_FILES[@]}")
	;;
*)
	echo "Unknown diff mode $2. Use 'pre-commit', 'all', or 'pull-request'"
	exit 1
	;;
esac

## split files into
##  - C++ source and header files,
##  - C++ test files (which have more lax linter rules)
##  - C-style header files (linted with different language standards)
SOURCE_FILES_TO_LINT=()
TEST_FILES_TO_LINT=()
C_HEADERS_TO_LINT=()
for FILE in "${FILES[@]}"; do
	if [[ $FILE == *external/* ]]; then
		continue
	elif [[ $FILE == *third_party/* ]]; then
		# skip third-party vendored dependencies
		continue
	elif [[ $FILE == *.h.in ]]; then
		# skip files used to generate C code
		continue
	elif [[ $FILE == *tools/mock_sysfs* && "$(uname -s)" != "Linux" ]]; then
		# mock_sysfs code only compiles on Linux, so skip these files if on other OS
		continue
	elif [[ $FILE == *.h ]]; then
		C_HEADERS_TO_LINT+=("$FILE")
	elif [[ $FILE == *tests/* || $FILE == *samples/* ]]; then
		TEST_FILES_TO_LINT+=("$FILE")
	else
		SOURCE_FILES_TO_LINT+=("$FILE")
	fi
done

EXTRA_ARGS=()
# exclude the C-level headers from this C++ lint - we'll do them as a separate step
EXTRA_ARGS+=(--extra-arg=-std=c++23)
# enable std::expected in clang-tidy
EXTRA_ARGS+=(--extra-arg=-D__cpp_concepts=202002L)
# set the version of the FUSE library. this should match the FUSE_USE_VERSION defined in tools/mock_sysfs/CMakeLists.txt
EXTRA_ARGS+=(--extra-arg=-DFUSE_USE_VERSION=316)
# if on x86_64, disable mmx intrinsics for linting to avoid issues with some CI runners
if [[ "$(uname -m)" == "x86_64" ]]; then
	EXTRA_ARGS+=(--extra-arg=-mno-mmx --extra-arg=-mno-sse --extra-arg=-mno-sse2)
fi

## Check for presense of libsensors, to determine if we should bother linting
## the libsensors examples
if echo '#include <sensors/sensors.h>
int main(void){return 0;}' | gcc -xc - -o /dev/null 2>/dev/null; then
	echo "libsensors header is available"
	EXTRA_ARGS+=(--extra-arg=-DASTL_INCLUDE_LIBSENSORS)
else
	echo "libsensors header is unavailable"
fi

PROTO_GENERATED_DIR="${BUILD_DIR}/src/impl/gen"
if [[ -d ${PROTO_GENERATED_DIR} ]]; then
	SYS_INCLUDE_PATHS+=(-isystem "${PROTO_GENERATED_DIR}")
	echo "Including generated protobuf headers from ${PROTO_GENERATED_DIR}"
else
	echo "Warning: Protobuf generated directory not found at ${PROTO_GENERATED_DIR}"
fi

if [[ ${#SOURCE_FILES_TO_LINT[@]} -gt 0 ]]; then
	echo "🧹 Linting C++ Sources"
	clang-tidy \
		"${SOURCE_FILES_TO_LINT[@]}" -p "${BUILD_DIR}" \
		-header-filter="'^(?!.*(include/astl|$BUILD_DIR/include/astl)).*'" \
		"${EXTRA_ARGS[@]}" \
		--warnings-as-errors=* \
		-- \
		"${INCLUDE_PATHS[@]}" \
		"${SYS_INCLUDE_PATHS[@]}"
fi

if [[ ${#TEST_FILES_TO_LINT[@]} -gt 0 ]]; then
	echo "🧹 Linting test files sources"
	clang-tidy \
		"${TEST_FILES_TO_LINT[@]}" -p "${BUILD_DIR}" \
		-header-filter="'^(?!.*(include/astl|$BUILD_DIR/include/astl)).*'" \
		"${EXTRA_ARGS[@]}" \
		-checks=-cppcoreguidelines-avoid-magic-numbers,-readability-magic-numbers,-readability-function-cognitive-complexity \
		-- \
		"${INCLUDE_PATHS[@]}" \
		"${SYS_INCLUDE_PATHS[@]}"
fi

# create a temporary C file to include all the C headers. This will cause clang-tidy to evaluate them as C headers
# (We had excluded them before to keep clang-tidy from trying to lint them as C++ headers)
if [[ ${#C_HEADERS_TO_LINT[@]} -gt 0 ]]; then
	echo "🧹 Linting C Headers"
	clang-tidy -header-filter="^(.*(include|$BUILD_DIR/include)).*" \
		"${C_HEADERS_TO_LINT[@]:-}" \
		-p "${BUILD_DIR}" \
		--warnings-as-errors=* \
		-checks=-cppcoreguidelines-macro-to-enum \
		-- \
		"${INCLUDE_PATHS[@]}" \
		"${SYS_INCLUDE_PATHS[@]}"
fi

echo "✅ Linting completed successfully."
