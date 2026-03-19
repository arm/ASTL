#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

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
	# On aarch64, skip GCC's system include directories (e.g., NEON headers) to avoid incompatibility with clang-tidy
	if [[ "$(uname -m)" == "aarch64" && $SYSTEM_INCLUDE == *"/gcc/"*"/include" ]]; then
		continue
	fi
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
CPP_FILE_PATTERN='\.(cpp|cc|cxx|c|h|hpp|hxx)$'
SHELL_FILE_PATTERN='(^|/)(justfile|[^/]+\.sh)$'

get_all_shell_lint_files() {
	git ls-files '*.sh' 'justfile'
}

get_staged_files() {
	git diff --cached --name-only --diff-filter=ACM | grep -E "${CPP_FILE_PATTERN}|${SHELL_FILE_PATTERN}" || true
}

get_diff_files() {
	# .github/workflows/integration.yml should set up the env variables for BASE_REF and HEAD_REF,
	# but provide reasonable default
	BASE_REF="${BASE_REF:-main}"
	CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
	HEAD_REF="${HEAD_REF:-${CURRENT_BRANCH}}"

	resolve_commit_ref() {
		local ref_name=$1
		for candidate in "$ref_name" "origin/$ref_name"; do
			if git rev-parse --verify --quiet "${candidate}^{commit}" >/dev/null; then
				echo "$candidate"
				return 0
			fi
		done
		return 1
	}

	FETCH_HEAD_PATH=$(git rev-parse --git-path FETCH_HEAD)
	FETCH_HEAD_DIR=$(dirname "$FETCH_HEAD_PATH")
	if [ ! -w "$FETCH_HEAD_DIR" ]; then
		echo "Skipping git fetch for lint diff selection: git metadata is read-only" >&2
	elif ! git fetch origin "$BASE_REF" "$HEAD_REF" >/dev/null 2>&1; then
		echo "Skipping git fetch for lint diff selection: unable to refresh refs" >&2
	fi

	BASE_DIFF_REF=$(resolve_commit_ref "$BASE_REF" || true)
	HEAD_DIFF_REF=$(resolve_commit_ref "$HEAD_REF" || true)
	if [ -z "$BASE_DIFF_REF" ] || [ -z "$HEAD_DIFF_REF" ]; then
		echo "Unable to resolve PR diff refs; falling back to linting all source files" >&2
		printf '%s\n' "${SOURCE_FILES[@]}"
		get_all_shell_lint_files
		return
	fi

	{
		git diff "$BASE_DIFF_REF...$HEAD_DIFF_REF" --name-only --diff-filter=ACM | grep -E "${CPP_FILE_PATTERN}|${SHELL_FILE_PATTERN}" || true
		git diff --cached --name-only --diff-filter=ACM | grep -E "${CPP_FILE_PATTERN}|${SHELL_FILE_PATTERN}" || true
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
	while IFS= read -r LINE; do
		FILES+=("$LINE")
	done < <(get_all_shell_lint_files)
	;;
*)
	echo "Unknown diff mode $2. Use 'pre-commit', 'all', or 'pull-request'"
	exit 1
	;;
esac

## split files into
##  - C++ source and header files,
##  - C++ test files (which have more lax linter rules)
##  - C source/test files
##  - C-style header files (linted with different language standards)
SOURCE_FILES_TO_LINT=()
SOURCE_HEADERS_TO_LINT=()
TEST_FILES_TO_LINT=()
TEST_HEADERS_TO_LINT=()
C_SOURCE_FILES_TO_LINT=()
C_HEADERS_TO_LINT=()
SHELL_FILES_TO_LINT=()
JUSTFILES_TO_LINT=()
for FILE in "${FILES[@]}"; do
	if [[ ! -e $FILE ]]; then
		echo "Skipping non-existent path from diff selection: $FILE"
		continue
	fi

	if [[ $FILE == *external/* ]]; then
		continue
	elif [[ $FILE == *third_party/* ]]; then
		# skip third-party vendored dependencies
		continue
	elif [[ $FILE == *justfile ]]; then
		JUSTFILES_TO_LINT+=("$FILE")
	elif [[ $FILE == *.sh ]]; then
		SHELL_FILES_TO_LINT+=("$FILE")
	elif [[ $FILE == *.h.in ]]; then
		# skip files used to generate C code
		continue
	elif [[ $FILE == *tools/mock_sysfs* && "$(uname -s)" != "Linux" ]]; then
		# mock_sysfs code only compiles on Linux, so skip these files if on other OS
		continue
	elif [[ $FILE == *.c ]]; then
		C_SOURCE_FILES_TO_LINT+=("$FILE")
	elif [[ $FILE == *.h ]]; then
		C_HEADERS_TO_LINT+=("$FILE")
	elif [[ $FILE == *tests/* || $FILE == *samples/* ]]; then
		if [[ $FILE == *.hpp || $FILE == *.hxx ]]; then
			TEST_HEADERS_TO_LINT+=("$FILE")
		else
			TEST_FILES_TO_LINT+=("$FILE")
		fi
	elif [[ $FILE == *.hpp || $FILE == *.hxx ]]; then
		SOURCE_HEADERS_TO_LINT+=("$FILE")
	elif [[ $FILE == *tests/* || $FILE == *samples/* ]]; then
		TEST_FILES_TO_LINT+=("$FILE")
	else
		SOURCE_FILES_TO_LINT+=("$FILE")
	fi
done
run_clang_tidy_batch() {
	local label=$1
	local checks=$2
	local -n files_ref=$3

	if [[ ${#files_ref[@]} -eq 0 ]]; then
		return 0
	fi

	local jobs
	jobs="${LINT_JOBS:-$(nproc)}"
	echo "🧹 ${label} (parallel jobs: ${jobs})"

	local clang_tidy_args=(
		-p "${BUILD_DIR}"
		-header-filter "^(?!.*(include/astl|${BUILD_DIR}/include/astl)).*"
		--warnings-as-errors=*
	)

	if [[ -n ${checks} ]]; then
		clang_tidy_args+=(-checks "${checks}")
	fi

	local extra_arg
	for extra_arg in "${EXTRA_ARGS[@]}"; do
		clang_tidy_args+=("-extra-arg=${extra_arg}")
	done

	local runner
	runner=$(mktemp)
	cat >"${runner}" <<'EOF'
#!/usr/bin/env bash
set -eu -o pipefail
file="${!#}"
set -- "${@:1:$(($# - 1))}"
clang-tidy "$file" "$@"
EOF
	chmod +x "${runner}"

	if ! printf '%s\0' "${files_ref[@]}" | xargs -0 -n 1 -P "${jobs}" "${runner}" \
		"${clang_tidy_args[@]}" \
		-- \
		"${INCLUDE_PATHS[@]}" \
		"${SYS_INCLUDE_PATHS[@]}"; then
		rm -f "${runner}"
		return 1
	fi

	rm -f "${runner}"
}

run_shell_lint() {
	if [[ ${#SHELL_FILES_TO_LINT[@]} -eq 0 ]]; then
		return 0
	fi

	if command -v shellcheck >/dev/null 2>&1; then
		echo "🧹 Linting shell scripts with shellcheck"
		shellcheck "${SHELL_FILES_TO_LINT[@]}"
		return 0
	fi

	echo "🧹 Linting shell scripts with bash -n (shellcheck unavailable)"
	local file
	for file in "${SHELL_FILES_TO_LINT[@]}"; do
		bash -n "$file"
	done
}

run_justfile_lint() {
	if [[ ${#JUSTFILES_TO_LINT[@]} -eq 0 ]]; then
		return 0
	fi

	if ! command -v just >/dev/null 2>&1; then
		echo "⚠️  Skipping justfile validation because 'just' is unavailable"
		return 0
	fi

	echo "🧹 Validating justfile syntax"
	local file
	for file in "${JUSTFILES_TO_LINT[@]}"; do
		just --justfile "$file" --dump >/dev/null
	done
}

EXTRA_ARGS=()
# exclude the C-level headers from this C++ lint - we'll do them as a separate step
EXTRA_ARGS+=(-std=c++23)
# enable std::expected in clang-tidy
EXTRA_ARGS+=(-D__cpp_concepts=202002L)
# set the version of the FUSE library. this should match the FUSE_USE_VERSION defined in tools/mock_sysfs/CMakeLists.txt
EXTRA_ARGS+=(-DFUSE_USE_VERSION=316)
# if on x86_64, disable mmx intrinsics for linting to avoid issues with some CI runners
if [[ "$(uname -m)" == "x86_64" ]]; then
	EXTRA_ARGS+=(-mno-mmx -mno-sse -mno-sse2)
fi

## Check for presense of libsensors, to determine if we should bother linting
## the libsensors examples
if echo '#include <sensors/sensors.h>
int main(void){return 0;}' | gcc -xc - -o /dev/null 2>/dev/null; then
	echo "libsensors header is available"
	EXTRA_ARGS+=(-DASTL_INCLUDE_LIBSENSORS)
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
	run_clang_tidy_batch "Linting C++ sources" "" SOURCE_FILES_TO_LINT
fi

if [[ ${#SOURCE_HEADERS_TO_LINT[@]} -gt 0 ]]; then
	run_clang_tidy_batch "Linting C++ headers" "" SOURCE_HEADERS_TO_LINT
fi

if [[ ${#TEST_FILES_TO_LINT[@]} -gt 0 ]]; then
	run_clang_tidy_batch \
		"Linting test sources" \
		"-cppcoreguidelines-avoid-magic-numbers,-readability-magic-numbers,-readability-function-cognitive-complexity" \
		TEST_FILES_TO_LINT
fi

if [[ ${#TEST_HEADERS_TO_LINT[@]} -gt 0 ]]; then
	run_clang_tidy_batch \
		"Linting test headers" \
		"-cppcoreguidelines-avoid-magic-numbers,-readability-magic-numbers,-readability-function-cognitive-complexity" \
		TEST_HEADERS_TO_LINT
fi

if [[ ${#C_SOURCE_FILES_TO_LINT[@]} -gt 0 ]]; then
	echo "🧹 Linting C sources"
	clang-tidy \
		"${C_SOURCE_FILES_TO_LINT[@]}" -p "${BUILD_DIR}" \
		--warnings-as-errors=* \
		-checks=-cppcoreguidelines-* \
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

run_shell_lint
run_justfile_lint

echo "✅ Linting completed successfully."
