#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

tag="${1:?usage: publish_public_release.sh release/VERSION}"
source_repo="Arm-Debug/ASTL"
target_repo="Arm/ASTL"

if [[ ! ${tag} =~ ^release/[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
	echo "Not a stable release tag: ${tag}" >&2
	exit 1
fi

work_dir="$(mktemp -d)"
trap 'rm -rf -- "${work_dir}"' EXIT

source_json="$(
	GH_TOKEN="${SOURCE_GITHUB_TOKEN:?SOURCE_GITHUB_TOKEN is required}" \
		gh release view "${tag}" --repo "${source_repo}" \
		--json name,body,isDraft,isPrerelease,assets
)"

if ! jq -e '
  .isDraft == false and .isPrerelease == false and
  any(.assets[]; .name == "release-complete.json")
' <<<"${source_json}" >/dev/null; then
	echo "Skipping incomplete release ${tag}."
	exit 0
fi

jq -j '.body // ""' <<<"${source_json}" >"${work_dir}/notes.md"
title="$(jq -r '.name // empty' <<<"${source_json}")"
title="${title:-${tag}}"

mkdir "${work_dir}/assets"
GH_TOKEN="${SOURCE_GITHUB_TOKEN}" \
	gh release download "${tag}" --repo "${source_repo}" --dir "${work_dir}/assets"

if GH_TOKEN="${TARGET_GITHUB_TOKEN:?TARGET_GITHUB_TOKEN is required}" \
	gh release view "${tag}" --repo "${target_repo}" >/dev/null 2>&1; then
	GH_TOKEN="${TARGET_GITHUB_TOKEN}" \
		gh release edit "${tag}" --repo "${target_repo}" --title "${title}" \
		--notes-file "${work_dir}/notes.md"
else
	GH_TOKEN="${TARGET_GITHUB_TOKEN}" \
		gh release create "${tag}" --repo "${target_repo}" --verify-tag \
		--title "${title}" --notes-file "${work_dir}/notes.md"
fi

shopt -s nullglob
assets=("${work_dir}/assets/"*)
if ((${#assets[@]})); then
	GH_TOKEN="${TARGET_GITHUB_TOKEN}" \
		gh release upload "${tag}" "${assets[@]}" --repo "${target_repo}" --clobber
fi
