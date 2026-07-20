#!/bin/sh

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -eu

log() {
	printf '%s\n' "$*"
}

fail() {
	printf 'Error: %s\n' "$*" >&2
	exit 1
}

usage() {
	cat <<'EOF'
Usage: sh install.sh [--help]

Options:
  --help     Show this help text.
EOF
}

require_tool() {
	if ! command -v "$1" >/dev/null 2>&1; then
		fail "required command not found: $1"
	fi
}

resolve_install_root() {
	scope="$1"
	kind="$2"

	if [ "$scope" = user ] && [ "${HOME:-}" = "" ]; then
		fail "cannot resolve user install root: HOME environment variable is not set"
	fi

	xdg_data_home="${XDG_DATA_HOME:-$HOME/.local/share}"

	case "$scope:$kind" in
	system:bin)
		printf '/usr/local/bin\n'
		;;
	system:lib)
		printf '/usr/local/lib\n'
		;;
	system:include)
		printf '/usr/local/include\n'
		;;
	system:config)
		printf '/usr/local/share/astl/config\n'
		;;
	system:install_state)
		printf '/usr/local/share/astl/install\n'
		;;
	system:samples)
		printf '/usr/local/share/astl/samples\n'
		;;
	user:bin)
		printf '%s/.local/bin\n' "$HOME"
		;;
	user:lib)
		printf '%s/.local/lib\n' "$HOME"
		;;
	user:include)
		printf '%s/.local/include\n' "$HOME"
		;;
	user:config)
		printf '%s/astl/config\n' "$xdg_data_home"
		;;
	user:install_state)
		printf '%s/astl/install\n' "$xdg_data_home"
		;;
	user:samples)
		printf '%s/astl/samples\n' "$xdg_data_home"
		;;
	*)
		fail "unsupported install root kind: $kind"
		;;
	esac
}

warn_optional_dependencies() {
	manifest_path="$1"

	python3 - "$manifest_path" <<'PY'
import json
import shutil
import sys

manifest = json.load(open(sys.argv[1], encoding="utf-8"))
for dependency in manifest.get("runtime_dependencies", {}).get("optional_commands", []):
    name = dependency["name"]
    if shutil.which(name) is None:
        reason = dependency.get("reason", "")
        if reason:
            print(f"Warning: optional dependency '{name}' is missing. {reason}", file=sys.stderr)
        else:
            print(f"Warning: optional dependency '{name}' is missing.", file=sys.stderr)
PY
}

emit_install_actions() {
	manifest_path="$1"

	python3 - "$manifest_path" <<'PY'
import json
import sys

manifest = json.load(open(sys.argv[1], encoding="utf-8"))

for entry in manifest.get("package_files", []):
    destination = entry.get("destination")
    if destination:
        print(
            "\t".join(
                [
                    "COPY",
                    entry["path"],
                    destination["kind"],
                    destination["relative_path"],
                    destination.get("mode", "0644"),
                ]
            )
        )

for artifact in manifest.get("installer_artifacts", []):
    kind = artifact["kind"]
    if kind == "copy":
        destination = artifact["destination"]
        print(
            "\t".join(
                [
                    "COPY",
                    artifact["source_path"],
                    destination["kind"],
                    destination["relative_path"],
                    destination.get("mode", "0644"),
                ]
            )
        )
    elif kind == "wrapper":
        destination = artifact["destination"]
        target = artifact["target"]
        print(
            "\t".join(
                [
                    "WRAPPER",
                    destination["kind"],
                    destination["relative_path"],
                    target["kind"],
                    target["relative_path"],
                    destination.get("mode", "0755"),
                ]
            )
        )
PY
}

validate_relative_path() {
	rel="$1"
	case "$rel" in
	/*)
		fail "manifest contains unsafe absolute path: $rel"
		;;
	.. | ../* | */.. | */../*)
		fail "manifest contains path traversal sequence: $rel"
		;;
	esac
}

resolve_self() {
	target="$1"
	while [ -L "$target" ]; do
		target_dir=$(CDPATH='' cd -- "$(dirname -- "$target")" && pwd)
		link_target=$(readlink "$target")
		case "$link_target" in
		/*)
			target="$link_target"
			;;
		*)
			target="$target_dir/$link_target"
			;;
		esac
	done
	target_dir=$(CDPATH='' cd -- "$(dirname -- "$target")" && pwd)
	printf '%s/%s\n' "$target_dir" "$(basename -- "$target")"
}

manifest_package_value() {
	manifest_path="$1"
	key="$2"

	python3 - "$manifest_path" "$key" <<'PY'
import json
import sys

manifest = json.load(open(sys.argv[1], encoding="utf-8"))
print(manifest["package"][sys.argv[2]])
PY
}

parse_args() {
	while [ "$#" -gt 0 ]; do
		case "$1" in
		-h | --help)
			usage
			exit 0
			;;
		*)
			fail "unknown argument: $1"
			;;
		esac
	done
}

main() {
	parse_args "$@"

	require_tool python3
	require_tool install

	SCRIPT_PATH=$(resolve_self "$0")
	PACKAGE_DIR=$(dirname "$SCRIPT_PATH")
	MANIFEST_PATH="$PACKAGE_DIR/manifest.json"
	[ -f "$MANIFEST_PATH" ] || fail "package is missing manifest.json"
	warn_optional_dependencies "$MANIFEST_PATH"
	INSTALLED_VERSION=$(manifest_package_value "$MANIFEST_PATH" "version")
	PACKAGE_OS=$(manifest_package_value "$MANIFEST_PATH" "os")
	PACKAGE_ARCH=$(manifest_package_value "$MANIFEST_PATH" "arch")

	if [ "$(id -u)" -eq 0 ]; then
		INSTALL_SCOPE="system"
	else
		INSTALL_SCOPE="user"
	fi

	INSTALL_STATE_DIR=$(resolve_install_root "$INSTALL_SCOPE" "install_state")
	if [ -f "$INSTALL_STATE_DIR/manifest.json" ] && [ -x "$INSTALL_STATE_DIR/uninstall.sh" ]; then
		log "Existing ASTL installation detected in $INSTALL_SCOPE scope. Removing it first."
		"$INSTALL_STATE_DIR/uninstall.sh" --quiet
	fi

	emit_install_actions "$MANIFEST_PATH" | while IFS="$(printf '\t')" read -r action field2 field3 field4 field5 field6; do
		case "$action" in
		COPY)
			source_rel="$field2"
			destination_kind="$field3"
			destination_rel="$field4"
			mode="$field5"
			validate_relative_path "$destination_rel"
			destination_root=$(resolve_install_root "$INSTALL_SCOPE" "$destination_kind")
			destination_path="$destination_root/$destination_rel"
			mkdir -p "$(dirname "$destination_path")"
			install -m "$mode" "$PACKAGE_DIR/$source_rel" "$destination_path"
			log "Installed $destination_path"
			;;
		WRAPPER)
			destination_kind="$field2"
			destination_rel="$field3"
			target_kind="$field4"
			target_rel="$field5"
			mode="$field6"
			validate_relative_path "$destination_rel"
			validate_relative_path "$target_rel"
			destination_root=$(resolve_install_root "$INSTALL_SCOPE" "$destination_kind")
			target_root=$(resolve_install_root "$INSTALL_SCOPE" "$target_kind")
			destination_path="$destination_root/$destination_rel"
			target_path="$target_root/$target_rel"
			mkdir -p "$(dirname "$destination_path")"
			cat >"$destination_path" <<EOF
#!/bin/sh
set -eu
exec "$target_path" "\$@"
EOF
			chmod "$mode" "$destination_path"
			log "Installed $destination_path"
			;;
		*)
			fail "unknown install action from manifest: $action"
			;;
		esac
	done

	if [ "$INSTALL_SCOPE" = "system" ] && command -v ldconfig >/dev/null 2>&1; then
		ldconfig || true
	fi

	log "ASTL ${INSTALLED_VERSION} (${PACKAGE_OS}_${PACKAGE_ARCH}) installed for $INSTALL_SCOPE scope."
	log "Uninstall with: $(resolve_install_root "$INSTALL_SCOPE" "bin")/astl-uninstall"
}

main "$@"
