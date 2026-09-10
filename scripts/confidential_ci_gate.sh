#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

readonly PRIVATE_REPOSITORY="Arm-Debug/ASTL-confidential"

required=(
	GH_TOKEN
	PUBLIC_GH_TOKEN
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

if [[ ! ${TARGET_REPOSITORY} =~ ^(Arm-Debug/ASTL|Arm/ASTL)$ ]]; then
	echo "Unsupported target repository: ${TARGET_REPOSITORY}" >&2
	exit 2
fi

if [[ ! ${HEAD_REPOSITORY} =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]]; then
	echo "Invalid head repository: ${HEAD_REPOSITORY}" >&2
	exit 2
fi
if [[ ! ${HEAD_SHA} =~ ^[0-9a-fA-F]{40}$ || ! ${BASE_SHA} =~ ^[0-9a-fA-F]{40}$ ]]; then
	echo "Head and base revisions must be full commit SHAs." >&2
	exit 2
fi
if [[ ! ${PR_NUMBER} =~ ^[0-9]+$ ]]; then
	echo "Invalid pull request number: ${PR_NUMBER}" >&2
	exit 2
fi
if [[ ! ${CORRELATION_ID} =~ ^[A-Za-z0-9_.-]+$ || ${#CORRELATION_ID} -gt 180 ]]; then
	echo "Invalid confidential CI correlation ID." >&2
	exit 2
fi

check_name="Confidential CI"
check_run="$(GH_TOKEN="${PUBLIC_GH_TOKEN}" gh api --method POST \
	-H "Accept: application/vnd.github+json" "repos/${TARGET_REPOSITORY}/check-runs" \
	-f name="${check_name}" -f head_sha="${HEAD_SHA}" -f status=in_progress \
	-f external_id="${CORRELATION_ID}" -f details_url="${PUBLIC_RUN_URL}" \
	-f 'output[title]=Confidential CI' \
	-f 'output[summary]=Dispatched to ASTL-confidential; awaiting callback.')"
check_run_id="$(jq -r '.id' <<<"${check_run}")"
[[ ${check_run_id} =~ ^[1-9][0-9]*$ ]] || {
	echo "Failed to create result check." >&2
	exit 1
}
fail_check() {
	GH_TOKEN="${PUBLIC_GH_TOKEN}" gh api --method PATCH \
		-H "Accept: application/vnd.github+json" "repos/${TARGET_REPOSITORY}/check-runs/${check_run_id}" \
		-f status=completed -f conclusion=failure \
		-f 'output[title]=Confidential CI dispatch failed' \
		-f 'output[summary]=ASTL could not dispatch the confidential workflow.' >/dev/null || true
}
trap fail_check ERR

payload="$({
	jq -n \
		--arg target_repository "${TARGET_REPOSITORY}" \
		--arg head_repository "${HEAD_REPOSITORY}" \
		--arg head_sha "${HEAD_SHA}" \
		--arg base_sha "${BASE_SHA}" \
		--arg pr_number "${PR_NUMBER}" \
		--arg correlation_id "${CORRELATION_ID}" \
		--arg check_run_id "${check_run_id}" \
		--arg check_name "${check_name}" \
		'{
      event_type: "astl-pr-confidential-ci",
      client_payload: {
        schema_version: 2,
        target_repository: $target_repository,
        head_repository: $head_repository,
        head_sha: $head_sha,
        base_sha: $base_sha,
        pr_number: $pr_number,
		correlation_id: $correlation_id,
		callback_repository: "Arm-Debug/ASTL",
		callback_event_type: "confidential-ci-result",
		check_run_id: $check_run_id,
		check_name: $check_name
      }
    }'
})"

echo "Dispatching confidential CI correlation ${CORRELATION_ID}."
gh api \
	--method POST \
	-H "Accept: application/vnd.github+json" \
	"repos/${PRIVATE_REPOSITORY}/dispatches" \
	--input - <<<"${payload}"
trap - ERR
echo "Confidential CI dispatched; result check ${check_run_id} awaits callback."
