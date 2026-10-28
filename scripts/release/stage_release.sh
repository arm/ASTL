#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd -P)"

usage() {
	cat <<'EOF'
Usage: scripts/release/stage_release.sh [OPTIONS]

Build, test, and stage ASTL release packages. No overlay is required.

Purpose and when to use it:
  Use stage_release.sh to validate and package an already-prepared source tree,
  either locally or during the publishing phase of release CI. It does not modify
  CHANGELOG.md or VERSION.md. The Prepare Release workflow runs prepare_release.py
  and opens a metadata PR first; after that PR is merged, the Release workflow
  invokes stage_release.sh to package the reviewed commit.

Choosing a release script:
  prepare_release.py  edits CHANGELOG.md and VERSION.md for a release commit.
  stage_release.sh    builds, tests, and packages without editing source metadata.

Options:
  --version VERSION             Package version (default: VERSION.md)
  --variant VARIANT             Repeat for library-only, everything, atx, or atx-static
                                (default: library-only)
  --output-dir DIR              Artifact output directory (default: build/release-stage)
  --release-profile NAME        Prepared release profile identity (default: public); isolates builds
  --config-include PATH          Keep this relative config subtree/file; repeat for profile allowlist
  --product NAME                Opaque product name; may be repeated
  --overlay-revision REVISION   Overlay source revision recorded in manifest.json
  --overlay-dirty               Mark the overlay source as dirty
  --confidential-config         Do not filter confidential JSON content
  --library-suffix SUFFIX       Library artifact suffix (default: library)
  --atx-extra-file SRC:DEST     Add a file to ATX packages; may be repeated
  --atx-extra-executable SRC:DEST
                                Add an executable file to ATX packages; may be repeated
  --config-replacement SRC:DEST Replace a config path BEFORE embedding for all binaries
  --skip-build                  Package an existing release build
  --skip-tests                  Do not run CTest or the optional MockScmi demo
  -h, --help                    Show this help
EOF
}

die() {
	echo "Error: $*" >&2
	exit 2
}

version=""
output_dir="${REPO_ROOT}/build/release-stage"
release_profile=""
overlay_revision=""
overlay_dirty=false
include_confidential=false
library_suffix="library"
skip_build=false
skip_tests=false
declare -a variants=()
declare -a products=()
declare -a config_includes=()
declare -a atx_extra_files=()
declare -a atx_extra_executables=()
declare -a config_replacements=()

while [[ $# -gt 0 ]]; do
	case "$1" in
	--version)
		[[ $# -ge 2 ]] || die "--version requires a value"
		version="$2"
		shift 2
		;;
	--variant)
		[[ $# -ge 2 ]] || die "--variant requires a value"
		variants+=("$2")
		shift 2
		;;
	--output-dir)
		[[ $# -ge 2 ]] || die "--output-dir requires a value"
		output_dir="$2"
		shift 2
		;;
	--release-profile)
		[[ $# -ge 2 ]] || die "--release-profile requires a value"
		release_profile="$2"
		shift 2
		;;
	--config-include)
		[[ $# -ge 2 ]] || die "--config-include requires a path"
		config_includes+=("$2")
		shift 2
		;;
	--product)
		[[ $# -ge 2 ]] || die "--product requires a value"
		products+=("$2")
		shift 2
		;;
	--overlay-revision)
		[[ $# -ge 2 ]] || die "--overlay-revision requires a value"
		overlay_revision="$2"
		shift 2
		;;
	--overlay-dirty)
		overlay_dirty=true
		shift
		;;
	--confidential-config)
		include_confidential=true
		shift
		;;
	--library-suffix)
		[[ $# -ge 2 ]] || die "--library-suffix requires a value"
		library_suffix="$2"
		shift 2
		;;
	--atx-extra-file)
		[[ $# -ge 2 ]] || die "--atx-extra-file requires SRC:DEST"
		atx_extra_files+=("$2")
		shift 2
		;;
	--atx-extra-executable)
		[[ $# -ge 2 ]] || die "--atx-extra-executable requires SRC:DEST"
		atx_extra_executables+=("$2")
		shift 2
		;;
	--config-replacement)
		[[ $# -ge 2 ]] || die "--config-replacement requires SRC:DEST"
		config_replacements+=("$2")
		shift 2
		;;
	--skip-build)
		skip_build=true
		shift
		;;
	--skip-tests)
		skip_tests=true
		shift
		;;
	-h | --help)
		usage
		exit 0
		;;
	*)
		die "unknown option: $1"
		;;
	esac
done

if [[ ${#variants[@]} -eq 0 ]]; then
	variants=("library-only")
fi
for variant in ${variants[@]+"${variants[@]}"}; do
	case "${variant}" in
	library-only | everything | atx | atx-static) ;;
	*) die "unsupported variant: ${variant}" ;;
	esac
done
[[ ${library_suffix} =~ ^[A-Za-z0-9_-]+$ ]] || die "invalid library suffix: ${library_suffix}"

if [[ -z ${version} ]]; then
	version="$(grep -m1 -v '^[[:space:]]*#' "${REPO_ROOT}/VERSION.md" | tr -d '\r' | xargs)"
fi
[[ ${version} =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][A-Za-z0-9.]+)?$ ]] || die "invalid version: ${version}"

for mapping in ${atx_extra_files[@]+"${atx_extra_files[@]}"} ${atx_extra_executables[@]+"${atx_extra_executables[@]}"} ${config_replacements[@]+"${config_replacements[@]}"}; do
	[[ -z ${mapping} || ${mapping} == *:* ]] || die "file mapping must use SRC:DEST: ${mapping}"
done

command -v cmake >/dev/null || die "cmake is required"
command -v python3 >/dev/null || die "python3 is required"
command -v jq >/dev/null || die "jq is required"
command -v zip >/dev/null || die "zip is required"
command -v sha256sum >/dev/null || die "sha256sum is required"

build_arch="$("${REPO_ROOT}"/scripts/host_arch.sh)"
case "${build_arch}" in
arm64) package_arch="aarch64" ;;
*) package_arch="${build_arch}" ;;
esac
case "$(uname -s)" in
Linux) os_name="linux" ;;
*) die "release packaging currently supports Linux only" ;;
esac

output_dir="$(mkdir -p "${output_dir}" && cd -- "${output_dir}" && pwd -P)"
release_profile="${release_profile:-public}"
[[ ${release_profile} =~ ^[A-Za-z0-9_-]+$ ]] || die "invalid release profile: ${release_profile}"
build_dir="${REPO_ROOT}/build/release-profiles/${release_profile}/release"
static_build_dir="${REPO_ROOT}/build/release-profiles/${release_profile}/static"
build_output_dir="${build_dir}/${build_arch}"
static_build_output_dir="${static_build_dir}/${build_arch}"
profile_suffix=""
[[ ${release_profile} == public ]] || profile_suffix="_${release_profile}"
published_config="${REPO_ROOT}/build/config-inputs/${release_profile}"
publish_args=(-o "${published_config}")
if [[ ${include_confidential} == true ]]; then
	publish_args+=(--confidential)
fi
"${REPO_ROOT}/scripts/publish_configs.sh" ${publish_args[@]+"${publish_args[@]}"}
# Detach overlay links even without an allowlist, before replacements can write.
python3 "${REPO_ROOT}/scripts/filter_config.py" "${published_config}" ${config_includes[@]+"${config_includes[@]}"}
json_count="$(find -L "${published_config}" -type f -name '*.json' | wc -l | xargs)"
[[ ${json_count} -gt 0 ]] || die "no JSON files were published"

for mapping in ${config_replacements[@]+"${config_replacements[@]}"}; do
	source="${mapping%%:*}"
	destination="${mapping#*:}"
	[[ ${source} != /* && ${source} != *'..'* && ${destination} != /* && ${destination} != *'..'* ]] ||
		die "config replacements must be safe relative paths"
	[[ -f ${published_config}/${source} ]] || die "replacement config not found: ${source}"
	mkdir -p "$(dirname "${published_config}/${destination}")"
	install -m 0644 "${published_config}/${source}" "${published_config}/${destination}"
done

# A skipped build must match the exact prepared input set, not just a profile label.
python3 "${REPO_ROOT}/scripts/embed_config.py" --root "${published_config}" \
	--output "${REPO_ROOT}/build/config-manifests/${release_profile}/expected.cpp" --profile "${release_profile}"
embedding_args=("-DASTL_EMBED_CONFIG_DIR=${published_config}"
	"-DASTL_RELEASE_PROFILE=${release_profile}")

contains_variant() {
	local expected="$1"
	local candidate
	for candidate in ${variants[@]+"${variants[@]}"}; do
		[[ ${candidate} == "${expected}" ]] && return 0
	done
	return 1
}

needs_static=false
contains_variant atx-static && needs_static=true

if [[ ${skip_build} == false ]]; then
	# Keep the normal release build independent from the fully-static ATX build.
	# ATX_STATIC_LINK disables libsensors, so it must never be enabled here.
	configure_args=(-S "${REPO_ROOT}" --preset release -B "${build_dir}" ${embedding_args[@]+"${embedding_args[@]}"} "-DASTL_VERSION_OVERRIDE=${version}"
		"-DATX_STATIC_LINK=false" "-DASTL_LIBSENSORS=AUTO")
	cmake ${configure_args[@]+"${configure_args[@]}"}
	cmake --build "${build_dir}" --parallel
	if [[ ${needs_static} == true ]]; then
		cmake -S "${REPO_ROOT}" --preset release-static -B "${static_build_dir}" ${embedding_args[@]+"${embedding_args[@]}"} "-DASTL_VERSION_OVERRIDE=${version}"
		cmake --build "${static_build_dir}" --parallel
	fi
fi

cmp -s "${REPO_ROOT}/build/config-manifests/${release_profile}/expected.json" "${build_dir}/generated/embedded_config.json" || die "embedded config does not match profile inputs; rebuild required"
if [[ ${needs_static} == true ]]; then
	cmp -s "${REPO_ROOT}/build/config-manifests/${release_profile}/expected.json" "${static_build_dir}/generated/embedded_config.json" || die "static embedded config does not match profile inputs; rebuild required"
fi

if [[ ${skip_build} == false ]]; then
	(cd "${build_output_dir}" && sha256sum lib/libastl-*.so lib/libastl_static-*.a) >"${build_dir}/embedded-binaries.sha256"
	if [[ ${needs_static} == true ]]; then
		(cd "${static_build_output_dir}" && sha256sum bin/atx-static) >"${static_build_dir}/embedded-binaries.sha256"
	fi
fi
(cd "${build_output_dir}" && sha256sum -c "${build_dir}/embedded-binaries.sha256") || die "profile binary verification failed; rebuild required"
if [[ ${needs_static} == true ]]; then
	(cd "${static_build_output_dir}" && sha256sum -c "${static_build_dir}/embedded-binaries.sha256") || die "static profile binary verification failed; rebuild required"
fi

[[ -d ${build_output_dir} ]] || die "release build output not found: ${build_output_dir}"
if [[ ${needs_static} == true ]]; then
	[[ -x ${static_build_output_dir}/bin/atx-static ]] ||
		die "static ATX build output not found: ${static_build_output_dir}/bin/atx-static"
fi

if [[ ${skip_tests} == false ]]; then
	# CTest discovers CMakePresets.json from its working directory, not --test-dir.
	# The confidential release wrapper may invoke this script from outside ASTL.
	(
		cd -- "${REPO_ROOT}"
		ctest --preset release --test-dir "${build_dir}"
	)
	# Embedded-mode smoke verification uses the profile's actual binary and no external config.
	if [[ -x ${build_output_dir}/bin/atx ]]; then
		env -u ASTL_CONFIG_DIR "${build_output_dir}/bin/atx" --version
	fi
fi

astl_revision="$(git -C "${REPO_ROOT}" rev-parse HEAD 2>/dev/null || true)"
astl_dirty=false
if [[ -n $(git -C "${REPO_ROOT}" status --porcelain --untracked-files=no 2>/dev/null || true) ]]; then
	astl_dirty=true
fi

manifest_args=(
	--version "${version}"
	--os "${os_name}"
	--arch "${package_arch}"
)
if [[ -n ${release_profile} ]]; then
	manifest_args+=(--release-profile "${release_profile}")
fi
for product in ${products[@]+"${products[@]}"}; do
	manifest_args+=(--product "${product}")
done
if [[ -n ${astl_revision} ]]; then
	manifest_args+=(--astl-revision "${astl_revision}")
	[[ ${astl_dirty} == true ]] && manifest_args+=(--astl-dirty)
fi
if [[ -n ${overlay_revision} ]]; then
	manifest_args+=(--overlay-revision "${overlay_revision}")
	[[ ${overlay_dirty} == true ]] && manifest_args+=(--overlay-dirty)
fi

copy_single_match() {
	local pattern="$1"
	local destination="$2"
	local -a matches=()
	while IFS= read -r match; do
		matches+=("${match}")
	done < <(compgen -G "${pattern}" || true)
	[[ ${#matches[@]} -eq 1 ]] || die "expected one file matching ${pattern}, found ${#matches[@]}"
	install -m 0644 "${matches[0]}" "${destination}/"
}

copy_pdfs() {
	local staging_dir="$1"
	local package_kind="$2"
	local copied=0
	local mapping source_dir destination_dir pdf
	local -a source_dirs=()
	if [[ ${package_kind} == library ]]; then
		source_dirs+=("${REPO_ROOT}/doc:${staging_dir}/doc")
		if [[ -d ${REPO_ROOT}/tools/ATX/doc ]]; then
			source_dirs+=("${REPO_ROOT}/tools/ATX/doc:${staging_dir}/doc/ATX")
		fi
	else
		source_dirs+=("${REPO_ROOT}/tools/ATX/doc:${staging_dir}/doc")
	fi
	for mapping in ${source_dirs[@]+"${source_dirs[@]}"}; do
		source_dir="${mapping%%:*}"
		destination_dir="${mapping#*:}"
		[[ -d ${source_dir} ]] || continue
		while IFS= read -r pdf; do
			[[ -n ${pdf} ]] || continue
			mkdir -p "${destination_dir}"
			install -m 0644 "${pdf}" "${destination_dir}/"
			copied=$((copied + 1))
		done < <(find "${source_dir}" -maxdepth 1 -type f -name '*.pdf' -print)
	done
	[[ ${copied} -gt 0 ]] || die "no PDF documentation found"
}

stage_library_base() {
	local staging_dir="$1"
	rm -rf -- "${staging_dir}"
	mkdir -p "${staging_dir}/lib" "${staging_dir}/include/astl"
	copy_single_match "${build_output_dir}/lib/libastl-*.so" "${staging_dir}/lib"
	copy_single_match "${build_output_dir}/lib/libastl_static-*.a" "${staging_dir}/lib"
	cp -L "${build_dir}/include/astl/"*.h "${staging_dir}/include/astl/"
	cp -L "${REPO_ROOT}/include/astl/"*.h "${staging_dir}/include/astl/"
	install -m 0644 "${build_dir}/generated/embedded_config.json" "${staging_dir}/embedded_config.json"
	copy_pdfs "${staging_dir}" library
	printf '%s\n' "${version}" >"${staging_dir}/VERSION.md"
	install -m 0755 "${REPO_ROOT}/scripts/release/install.sh" "${staging_dir}/install.sh"
	install -m 0755 "${REPO_ROOT}/scripts/release/uninstall.sh" "${staging_dir}/uninstall.sh"
}

stage_everything() {
	local staging_dir="$1"
	local library_dir="$2"
	rm -rf -- "${staging_dir}"
	mkdir -p "${staging_dir}"
	cp -a "${library_dir}/." "${staging_dir}/"
	mkdir -p "${staging_dir}/bin"
	install -m 0755 "${build_output_dir}/bin/atx" "${staging_dir}/bin/atx"
	if [[ -x ${build_output_dir}/bin/MockScmi ]]; then
		install -m 0755 "${build_output_dir}/bin/MockScmi" "${staging_dir}/bin/MockScmi"
	fi
	if [[ -d ${REPO_ROOT}/tools/ATX/config ]]; then
		mkdir -p "${staging_dir}/bin/config/atx/"
		cp -a "${REPO_ROOT}/tools/ATX/config/." "${staging_dir}/bin/config/atx/"
	fi
	if [[ -d ${REPO_ROOT}/samples/sample_test ]]; then
		mkdir -p "${staging_dir}/cmake" "${staging_dir}/samples"
		cp -aL "${REPO_ROOT}/cmake/Modules" "${staging_dir}/cmake/"
		cp -aL "${REPO_ROOT}/samples/sample_test" "${staging_dir}/samples/"
	fi
}

apply_atx_extras() {
	local staging_dir="$1"
	local mapping source destination
	for mapping in ${atx_extra_files[@]+"${atx_extra_files[@]}"}; do
		source="${mapping%%:*}"
		destination="${mapping#*:}"
		[[ -f ${source} ]] || die "ATX extra file not found: ${source}"
		[[ ${destination} != /* && ${destination} != *'..'* ]] || die "invalid ATX destination: ${destination}"
		mkdir -p "$(dirname "${staging_dir}/${destination}")"
		install -m 0644 "${source}" "${staging_dir}/${destination}"
	done
	for mapping in ${atx_extra_executables[@]+"${atx_extra_executables[@]}"}; do
		source="${mapping%%:*}"
		destination="${mapping#*:}"
		[[ -f ${source} ]] || die "ATX extra executable not found: ${source}"
		[[ ${destination} != /* && ${destination} != *'..'* ]] || die "invalid ATX destination: ${destination}"
		mkdir -p "$(dirname "${staging_dir}/${destination}")"
		install -m 0755 "${source}" "${staging_dir}/${destination}"
	done

}

stage_atx() {
	local staging_dir="$1"
	rm -rf -- "${staging_dir}"
	mkdir -p "${staging_dir}/bin" "${staging_dir}/lib"
	install -m 0755 "${build_output_dir}/bin/atx" "${staging_dir}/bin/atx"
	copy_single_match "${build_output_dir}/lib/libastl-*.so" "${staging_dir}/lib"
	install -m 0644 "${build_dir}/generated/embedded_config.json" "${staging_dir}/embedded_config.json"
	if [[ -d ${REPO_ROOT}/tools/ATX/config ]]; then
		mkdir -p "${staging_dir}/bin/config/atx/"
		cp -a "${REPO_ROOT}/tools/ATX/config/." "${staging_dir}/bin/config/atx/"
	fi
	copy_pdfs "${staging_dir}" atx
	printf '%s\n' "${version}" >"${staging_dir}/VERSION.md"
	apply_atx_extras "${staging_dir}"
}

archive_stage() {
	local staging_dir="$1"
	local manifest_variant="$2"
	local artifact_name
	artifact_name="$(basename "${staging_dir}")"
	python3 "${REPO_ROOT}/scripts/generate_release_manifest.py" \
		--staging-dir "${staging_dir}" \
		--variant "${manifest_variant}" \
		${manifest_args[@]+"${manifest_args[@]}"}
	(
		cd "${output_dir}"
		rm -f -- "${artifact_name}.zip" "${artifact_name}.zip.sha256"
		zip -q -r -9 "${artifact_name}.zip" "${artifact_name}"
		sha256sum "${artifact_name}.zip" >"${artifact_name}.zip.sha256"
	)
	echo "Created ${output_dir}/${artifact_name}.zip"
}

library_name="astl_version_${version}_${os_name}_${package_arch}${profile_suffix}_${library_suffix}"
library_stage="${output_dir}/${library_name}"
stage_library_base "${library_stage}"

if contains_variant library-only; then
	archive_stage "${library_stage}" "library_only"
fi

if contains_variant everything; then
	everything_name="astl_version_${version}_${os_name}_${package_arch}${profile_suffix}_everything"
	everything_stage="${output_dir}/${everything_name}"
	stage_everything "${everything_stage}" "${library_stage}"
	archive_stage "${everything_stage}" "everything"
fi

if contains_variant atx || contains_variant atx-static; then
	atx_name="atx_version_${version}_${os_name}_${package_arch}${profile_suffix}"
	atx_stage="${output_dir}/${atx_name}"
	stage_atx "${atx_stage}"
	if contains_variant atx; then
		archive_stage "${atx_stage}" "atx"
	fi
	if contains_variant atx-static; then
		static_name="atx_version_${version}_${os_name}_${package_arch}${profile_suffix}_static"
		static_stage="${output_dir}/${static_name}"
		rm -rf -- "${static_stage}"
		mkdir -p "${static_stage}"
		cp -a "${atx_stage}/." "${static_stage}/"
		install -m 0755 "${static_build_output_dir}/bin/atx-static" "${static_stage}/bin/atx"
		find "${static_stage}/lib" -maxdepth 1 -type f -name 'libastl-*.so' -delete
		"${REPO_ROOT}/scripts/verify_static_executable.sh" "${static_stage}/bin/atx"
		"${static_stage}/bin/atx" --version
		archive_stage "${static_stage}" "atx_static"
	fi
fi

if ! contains_variant library-only && ! contains_variant everything; then
	rm -rf -- "${library_stage}"
fi

echo "Release staging complete: ${output_dir}"
