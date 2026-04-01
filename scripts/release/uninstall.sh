#!/bin/sh

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -eu

QUIET=0

log() {
	if [ "$QUIET" -eq 0 ]; then
		printf '%s\n' "$*"
	fi
}

fail() {
	printf 'Error: %s\n' "$*" >&2
	exit 1
}

usage() {
	cat <<'EOF'
Usage: uninstall.sh [--quiet] [--help]

Options:
  --quiet  Suppress normal progress output.
  --help   Show this help text.
EOF
}

parse_args() {
	while [ "$#" -gt 0 ]; do
		case "$1" in
		--quiet)
			QUIET=1
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

resolve_user_install_state() {
	xdg_data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
	printf '%s/astl/install\n' "$xdg_data_home"
}

resolve_system_install_state() {
	printf '/usr/local/share/astl/install\n'
}

resolve_install_root() {
	scope="$1"
	kind="$2"
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

detect_scope_from_script_path() {
	script_path="$1"
	script_dir=$(dirname "$script_path")
	if [ "$script_dir" = "$(resolve_system_install_state)" ]; then
		printf 'system\n'
		return
	fi
	if [ "$script_dir" = "$(resolve_user_install_state)" ]; then
		printf 'user\n'
		return
	fi
	fail "unable to determine ASTL install scope from $script_dir"
}

emit_removal_actions() {
	manifest_path="$1"

	python3 - "$manifest_path" <<'PY'
import json
import sys

manifest = json.load(open(sys.argv[1], encoding="utf-8"))

for entry in manifest.get("package_files", []):
    destination = entry.get("destination")
    if destination:
        print("\t".join(["REMOVE", destination["kind"], destination["relative_path"]]))

for artifact in manifest.get("installer_artifacts", []):
    destination = artifact["destination"]
    print("\t".join(["REMOVE", destination["kind"], destination["relative_path"]]))
PY
}

main() {
	parse_args "$@"

	command -v python3 >/dev/null 2>&1 || fail "required command not found: python3"

	SCRIPT_PATH=$(resolve_self "$0")
	SCRIPT_DIR=$(dirname "$SCRIPT_PATH")
	INSTALL_SCOPE=$(detect_scope_from_script_path "$SCRIPT_PATH")
	MANIFEST_PATH="$SCRIPT_DIR/manifest.json"

	[ -f "$MANIFEST_PATH" ] || fail "manifest not found at $MANIFEST_PATH"

	emit_removal_actions "$MANIFEST_PATH" | while IFS="$(printf '\t')" read -r action destination_kind destination_rel; do
		[ "$action" = "REMOVE" ] || fail "unknown uninstall action: $action"
		validate_relative_path "$destination_rel"
		destination_root=$(resolve_install_root "$INSTALL_SCOPE" "$destination_kind")
		destination_path="$destination_root/$destination_rel"
		if [ -L "$destination_path" ] || [ -f "$destination_path" ]; then
			rm -f "$destination_path"
			log "Removed $destination_path"
		fi
	done

	if [ "$INSTALL_SCOPE" = "system" ] && command -v ldconfig >/dev/null 2>&1; then
		ldconfig || true
	fi

	for directory in \
		"$(resolve_install_root "$INSTALL_SCOPE" "config")" \
		"$(resolve_install_root "$INSTALL_SCOPE" "install_state")" \
		"$(resolve_install_root "$INSTALL_SCOPE" "samples")" \
		"/usr/local/share/astl" \
		"${XDG_DATA_HOME:-$HOME/.local/share}/astl"; do
		rm -r "$directory" 2>/dev/null || true
		log "Removed $directory"
	done

	log "ASTL uninstall completed for $INSTALL_SCOPE scope."
}

main "$@"
