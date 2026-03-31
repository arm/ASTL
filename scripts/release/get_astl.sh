#!/bin/sh

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -eu

REPO_OWNER="Arm-Debug"
REPO_NAME="ASTL"
DEFAULT_SOURCE="github"
DEFAULT_VERSION="latest"
PACKAGE_VARIANT="everything"

supports_authenticated_gh() {
	command -v gh >/dev/null 2>&1 && gh auth status -h github.com >/dev/null 2>&1
}

log() {
	printf '%s\n' "$*"
}

fail() {
	printf 'Error: %s\n' "$*" >&2
	exit 1
}

usage() {
	cat <<'EOF'
Usage: sh get_astl.sh [--source github] [--version latest|X.Y.Z] [--os linux] [--arch aarch64] [--help]

Options:
  --source   Package source. Only "github" is implemented right now.
  --version  ASTL version to install. Default: latest (maps to release/rolling).
  --os       Package OS. Default: detect from current system.
  --arch     Package architecture. Default: detect from current system.
  --help     Show this help text.

Notes:
  If repository releases are private, authenticate gh first (gh auth login).
EOF
}

require_tool() {
	if ! command -v "$1" >/dev/null 2>&1; then
		fail "required command not found: $1"
	fi
}

resolve_os() {
	case "$(uname -s)" in
	Linux)
		printf 'linux\n'
		;;
	*)
		fail "unsupported operating system: $(uname -s). Linux is the only supported platform right now."
		;;
	esac
}

resolve_arch() {
	case "$(uname -m)" in
	aarch64 | arm64)
		printf 'aarch64\n'
		;;
	*)
		fail "unsupported architecture: $(uname -m). Only linux_aarch64 releases are published right now."
		;;
	esac
}

github_api_asset_info() {
	release_json_path="$1"
	os_name="$2"
	arch="$3"
	variant="$4"

	python3 - "$release_json_path" "$os_name" "$arch" "$variant" <<'PY'
import json
import re
import sys

release_json_path, os_name, arch, variant = sys.argv[1:5]
with open(release_json_path, encoding="utf-8") as handle:
    release = json.load(handle)

asset_pattern = re.compile(
    rf"^astl_version_.+_{re.escape(os_name)}_{re.escape(arch)}_{re.escape(variant)}\.zip$"
)
checksum_pattern = re.compile(
    rf"^astl_version_.+_{re.escape(os_name)}_{re.escape(arch)}_{re.escape(variant)}\.zip\.sha256$"
)

zip_asset = None
checksum_asset = None
for asset in release.get("assets", []):
    name = asset.get("name", "")
    if asset_pattern.match(name):
        zip_asset = asset
    elif checksum_pattern.match(name):
        checksum_asset = asset

if zip_asset is None:
    raise SystemExit("release does not contain a matching ASTL package asset")
if checksum_asset is None:
    raise SystemExit("release does not contain a matching ASTL checksum asset")

print(zip_asset["name"])
print(zip_asset["browser_download_url"])
print(checksum_asset["name"])
print(checksum_asset["browser_download_url"])
PY
}

validate_manifest_against_package() {
	manifest_path="$1"
	package_dir="$2"

	python3 - "$manifest_path" "$package_dir" <<'PY'
import hashlib
import json
import os
import sys
from pathlib import Path

manifest_path = Path(sys.argv[1])
package_dir = Path(sys.argv[2])
manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

manifest_entries = {entry["path"]: entry for entry in manifest.get("package_files", [])}
actual_entries = {}
for path in sorted(package_dir.rglob("*")):
    if path.is_dir():
        continue
    relative_path = path.relative_to(package_dir).as_posix()
    actual_entries[relative_path] = path

expected_paths = set(manifest_entries)
expected_paths.add("manifest.json")
actual_paths = set(actual_entries)

missing = sorted(expected_paths - actual_paths)
unexpected = sorted(actual_paths - expected_paths)
if missing:
    raise SystemExit(f"manifest validation failed; missing files: {missing}")
if unexpected:
    raise SystemExit(f"manifest validation failed; unexpected files: {unexpected}")

for relative_path, entry in manifest_entries.items():
    path = actual_entries[relative_path]
    if entry["type"] == "symlink":
        if not path.is_symlink():
            raise SystemExit(f"expected symlink, found regular file: {relative_path}")
        actual_target = os.readlink(path)
        if actual_target != entry["link_target"]:
            raise SystemExit(
                f"symlink target mismatch for {relative_path}: expected {entry['link_target']}, got {actual_target}"
            )
        continue

    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    if digest.hexdigest() != entry["sha256"]:
        raise SystemExit(f"sha256 mismatch for {relative_path}")
PY
}

parse_args() {
	SOURCE="$DEFAULT_SOURCE"
	VERSION="$DEFAULT_VERSION"
	OS_NAME=""
	ARCH=""

	while [ "$#" -gt 0 ]; do
		case "$1" in
		--source)
			[ "$#" -ge 2 ] || fail "--source requires a value"
			SOURCE="$2"
			shift 2
			;;
		--source=*)
			SOURCE=${1#*=}
			shift
			;;
		--version)
			[ "$#" -ge 2 ] || fail "--version requires a value"
			VERSION="$2"
			shift 2
			;;
		--version=*)
			VERSION=${1#*=}
			shift
			;;
		--os)
			[ "$#" -ge 2 ] || fail "--os requires a value"
			OS_NAME="$2"
			shift 2
			;;
		--os=*)
			OS_NAME=${1#*=}
			shift
			;;
		--arch)
			[ "$#" -ge 2 ] || fail "--arch requires a value"
			ARCH="$2"
			shift 2
			;;
		--arch=*)
			ARCH=${1#*=}
			shift
			;;
		-h | --help)
			usage
			exit 0
			;;
		*)
			fail "unknown argument: $1"
			;;
		esac
	done

	if [ -z "$OS_NAME" ]; then
		OS_NAME=$(resolve_os)
	fi
	if [ -z "$ARCH" ]; then
		ARCH=$(resolve_arch)
	fi
}

main() {
	parse_args "$@"

	if [ "$SOURCE" != "github" ]; then
		fail "source '$SOURCE' is not implemented yet. Only 'github' is supported."
	fi

	require_tool curl
	require_tool unzip
	require_tool sha256sum
	require_tool python3
	require_tool mktemp
	require_tool find
	require_tool sed

	if [ "$VERSION" = "latest" ]; then
		TAG_NAME="release/rolling"
	else
		TAG_NAME="release/$VERSION"
	fi

	TMP_DIR=$(mktemp -d)
	trap 'rm -rf "$TMP_DIR"' EXIT HUP INT TERM

	DOWNLOAD_DIR="$TMP_DIR/download"
	EXTRACT_DIR="$TMP_DIR/extracted"
	mkdir -p "$DOWNLOAD_DIR" "$EXTRACT_DIR"

	ZIP_PATTERN="astl_version_*_${OS_NAME}_${ARCH}_${PACKAGE_VARIANT}.zip"
	CHECKSUM_PATTERN="${ZIP_PATTERN}.sha256"

	if supports_authenticated_gh; then
		log "Using authenticated gh CLI to download release assets"
		gh release download "$TAG_NAME" \
			--repo "$REPO_OWNER/$REPO_NAME" \
			--pattern "$ZIP_PATTERN" \
			--pattern "$CHECKSUM_PATTERN" \
			--dir "$DOWNLOAD_DIR" \
			--clobber >/dev/null

		ZIP_COUNT=$(find "$DOWNLOAD_DIR" -maxdepth 1 -type f -name "$ZIP_PATTERN" | wc -l | tr -d ' ')
		[ "$ZIP_COUNT" = "1" ] || fail "expected exactly one package asset matching $ZIP_PATTERN, found $ZIP_COUNT"
		CHECKSUM_COUNT=$(find "$DOWNLOAD_DIR" -maxdepth 1 -type f -name "$CHECKSUM_PATTERN" | wc -l | tr -d ' ')
		[ "$CHECKSUM_COUNT" = "1" ] || fail "expected exactly one checksum asset matching $CHECKSUM_PATTERN, found $CHECKSUM_COUNT"

		ZIP_PATH=$(find "$DOWNLOAD_DIR" -maxdepth 1 -type f -name "$ZIP_PATTERN" | sed -n '1p')
		CHECKSUM_PATH=$(find "$DOWNLOAD_DIR" -maxdepth 1 -type f -name "$CHECKSUM_PATTERN" | sed -n '1p')
		ZIP_NAME=$(basename "$ZIP_PATH")
		CHECKSUM_NAME=$(basename "$CHECKSUM_PATH")
	else
		TAG_PATH=$(python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1], safe=""))' "$TAG_NAME")
		RELEASE_JSON="$TMP_DIR/release.json"
		RELEASE_URL="https://api.github.com/repos/$REPO_OWNER/$REPO_NAME/releases/tags/$TAG_PATH"

		if ! curl -fsSL -H 'Accept: application/vnd.github+json' "$RELEASE_URL" -o "$RELEASE_JSON"; then
			fail "unable to access release metadata via GitHub API. If this repository is private, authenticate with 'gh auth login' and rerun."
		fi

		ASSET_INFO="$TMP_DIR/asset-info.txt"
		github_api_asset_info "$RELEASE_JSON" "$OS_NAME" "$ARCH" "$PACKAGE_VARIANT" >"$ASSET_INFO"
		ZIP_NAME=$(sed -n '1p' "$ASSET_INFO")
		ZIP_URL=$(sed -n '2p' "$ASSET_INFO")
		CHECKSUM_NAME=$(sed -n '3p' "$ASSET_INFO")
		CHECKSUM_URL=$(sed -n '4p' "$ASSET_INFO")

		log "Downloading $ZIP_NAME"
		curl -fsSL "$ZIP_URL" -o "$DOWNLOAD_DIR/$ZIP_NAME"
		curl -fsSL "$CHECKSUM_URL" -o "$DOWNLOAD_DIR/$CHECKSUM_NAME"

		ZIP_PATH="$DOWNLOAD_DIR/$ZIP_NAME"
		CHECKSUM_PATH="$DOWNLOAD_DIR/$CHECKSUM_NAME"
	fi

	(
		cd "$DOWNLOAD_DIR"
		sha256sum -c "$CHECKSUM_NAME"
	)

	unzip -q "$ZIP_PATH" -d "$EXTRACT_DIR"
	PACKAGE_DIR_COUNT=$(find "$EXTRACT_DIR" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')
	[ "$PACKAGE_DIR_COUNT" = "1" ] || fail "expected exactly one package directory after extraction, found $PACKAGE_DIR_COUNT"
	PACKAGE_DIR=$(find "$EXTRACT_DIR" -mindepth 1 -maxdepth 1 -type d | sed -n '1p')

	MANIFEST_PATH="$PACKAGE_DIR/manifest.json"
	[ -f "$MANIFEST_PATH" ] || fail "package is missing manifest.json"
	validate_manifest_against_package "$MANIFEST_PATH" "$PACKAGE_DIR"

	[ -x "$PACKAGE_DIR/install.sh" ] || fail "package is missing executable install.sh"
	"$PACKAGE_DIR/install.sh"
}

main "$@"
