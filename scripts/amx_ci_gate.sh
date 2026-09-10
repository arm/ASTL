#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0
# shellcheck disable=SC2153  # Environment names are validated indirectly below.

set -euo pipefail

readonly PRIVATE_REPOSITORY="Arm-Debug/ASTL-confidential"

required=(
	GH_TOKEN
	PUBLIC_GH_TOKEN
	SOURCE_EVENT
	TARGET_REPOSITORY
	HEAD_REPOSITORY
	HEAD_SHA
	BASE_SHA
	PR_NUMBER
	CORRELATION_ID
	PUBLIC_RUN_URL
)
for name in "${required[@]}"; do
	if [[ -z ${!name:-} ]]; then
		echo "Required environment variable is empty: ${name}" >&2
		exit 2
	fi
done

source_event="${SOURCE_EVENT}"
target_repository="${TARGET_REPOSITORY}"
head_repository="${HEAD_REPOSITORY}"
head_sha="$(tr 'A-F' 'a-f' <<<"${HEAD_SHA}")"
base_sha="$(tr 'A-F' 'a-f' <<<"${BASE_SHA}")"
pr_number="${PR_NUMBER}"
correlation_id="${CORRELATION_ID}"

if [[ ! ${source_event} =~ ^(pull_request|push)$ ]]; then
	echo "Unsupported source event: ${source_event}" >&2
	exit 2
fi
if [[ ${target_repository} != "Arm-Debug/ASTL" ]]; then
	echo "Unsupported target repository: ${target_repository}" >&2
	exit 2
fi
if [[ ! ${head_repository} =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]]; then
	echo "Invalid head repository: ${head_repository}" >&2
	exit 2
fi
if [[ ! ${head_sha} =~ ^[0-9a-f]{40}$ || ! ${base_sha} =~ ^[0-9a-f]{40}$ ]]; then
	echo "Head and base revisions must be full commit SHAs." >&2
	exit 2
fi
if [[ ! ${pr_number} =~ ^[0-9]+$ ]]; then
	echo "Invalid pull request number: ${pr_number}" >&2
	exit 2
fi
if [[ ${source_event} == "pull_request" && ${pr_number} == "0" ]]; then
	echo "Pull-request AMX CI requires a non-zero pull request number." >&2
	exit 2
fi
if [[ ${source_event} == "push" &&
	(${pr_number} != "0" || ${head_repository} != "${target_repository}") ]]; then
	echo "Push AMX CI requires pr_number=0 and matching target/head repositories." >&2
	exit 2
fi
if [[ ! ${correlation_id} =~ ^[A-Za-z0-9_.-]+$ || ${#correlation_id} -gt 180 ]]; then
	echo "Invalid AMX CI correlation ID." >&2
	exit 2
fi

check_name="AMX CI / aggregate"
check_run="$(GH_TOKEN="${PUBLIC_GH_TOKEN}" gh api --method POST \
	-H "Accept: application/vnd.github+json" "repos/${target_repository}/check-runs" \
	-f name="${check_name}" -f head_sha="${head_sha}" -f status=in_progress \
	-f external_id="${correlation_id}" -f details_url="${PUBLIC_RUN_URL}" \
	-f 'output[title]=AMX CI / aggregate' \
	-f 'output[summary]=Dispatched to ASTL-confidential; awaiting callback.')"
check_run_id="$(jq -r '.id' <<<"${check_run}")"
[[ ${check_run_id} =~ ^[1-9][0-9]*$ ]] || {
	echo "Failed to create result check." >&2
	exit 1
}
fail_check() {
	GH_TOKEN="${PUBLIC_GH_TOKEN}" gh api --method PATCH \
		-H "Accept: application/vnd.github+json" "repos/${target_repository}/check-runs/${check_run_id}" \
		-f status=completed -f conclusion=failure \
		-f 'output[title]=AMX CI dispatch failed' \
		-f 'output[summary]=ASTL could not dispatch the AMX workflow.' >/dev/null || true
}
trap fail_check ERR

payload="$(jq -n \
	--arg source_event "${source_event}" \
	--arg target_repository "${target_repository}" \
	--arg head_repository "${head_repository}" \
	--arg head_sha "${head_sha}" \
	--arg base_sha "${base_sha}" \
	--arg pr_number "${pr_number}" \
	--arg correlation_id "${correlation_id}" \
	--arg check_run_id "${check_run_id}" \
	--arg check_name "${check_name}" \
	'{
      event_type: "amx-ci-public-change",
      client_payload: {
        schema_version: 2,
        source_event: $source_event,
        target_repository: $target_repository,
        head_repository: $head_repository,
        head_sha: $head_sha,
        base_sha: $base_sha,
        pr_number: $pr_number,
		correlation_id: $correlation_id,
		callback_repository: "Arm-Debug/ASTL",
		callback_event_type: "amx-ci-result",
		check_run_id: $check_run_id,
		check_name: $check_name
      }
    }')"

echo "Dispatching AMX CI correlation ${correlation_id}."
gh api \
	--method POST \
	-H "Accept: application/vnd.github+json" \
	"repos/${PRIVATE_REPOSITORY}/dispatches" \
	--input - <<<"${payload}"
trap - ERR
echo "AMX CI dispatched; result check ${check_run_id} awaits callback."
