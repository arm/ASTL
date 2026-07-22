#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

readonly PRIVATE_REPOSITORY="Arm-Debug/ASTL-confidential"
readonly PRIVATE_WORKFLOW="functional-dispatcher.yml"
readonly POLL_INTERVAL_SECONDS=15
readonly MAX_POLLS=720

required=(
	GH_TOKEN
	TARGET_REPOSITORY
	HEAD_REPOSITORY
	HEAD_SHA
	BASE_SHA
	PR_NUMBER
	CORRELATION_ID
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

payload="$({
	jq -n \
		--arg target_repository "${TARGET_REPOSITORY}" \
		--arg head_repository "${HEAD_REPOSITORY}" \
		--arg head_sha "${HEAD_SHA}" \
		--arg base_sha "${BASE_SHA}" \
		--arg pr_number "${PR_NUMBER}" \
		--arg correlation_id "${CORRELATION_ID}" \
		'{
      event_type: "astl-pr-confidential-ci",
      client_payload: {
        target_repository: $target_repository,
        head_repository: $head_repository,
        head_sha: $head_sha,
        base_sha: $base_sha,
        pr_number: $pr_number,
        correlation_id: $correlation_id
      }
    }'
})"

echo "Dispatching confidential CI correlation ${CORRELATION_ID}."
gh api \
	--method POST \
	-H "Accept: application/vnd.github+json" \
	"repos/${PRIVATE_REPOSITORY}/dispatches" \
	--input - <<<"${payload}"

run_id=""
for ((poll = 1; poll <= MAX_POLLS; poll++)); do
	runs="$(gh api \
		-H "Accept: application/vnd.github+json" \
		"repos/${PRIVATE_REPOSITORY}/actions/workflows/${PRIVATE_WORKFLOW}/runs?event=repository_dispatch&per_page=100")"
	run_id="$(jq -r \
		--arg title "Confidential CI ${CORRELATION_ID}" \
		'.workflow_runs[] | select(.display_title == $title) | .id' \
		<<<"${runs}" | head -n 1)"
	if [[ -n ${run_id} ]]; then
		break
	fi
	sleep "${POLL_INTERVAL_SECONDS}"
done

if [[ -z ${run_id} ]]; then
	echo "Timed out waiting for the confidential workflow to start." >&2
	exit 1
fi

for ((poll = 1; poll <= MAX_POLLS; poll++)); do
	run="$(gh api \
		-H "Accept: application/vnd.github+json" \
		"repos/${PRIVATE_REPOSITORY}/actions/runs/${run_id}")"
	status="$(jq -r '.status' <<<"${run}")"
	conclusion="$(jq -r '.conclusion // ""' <<<"${run}")"
	details_url="$(jq -r '.html_url' <<<"${run}")"
	if [[ ${status} == "completed" ]]; then
		{
			echo "### Confidential CI"
			echo "- Correlation: \`${CORRELATION_ID}\`"
			echo "- Private result: ${conclusion}"
			echo "- [Internal workflow details](${details_url})"
		} >>"${GITHUB_STEP_SUMMARY}"
		if [[ ${conclusion} == "success" ]]; then
			exit 0
		fi
		echo "Confidential CI concluded with: ${conclusion}" >&2
		exit 1
	fi
	sleep "${POLL_INTERVAL_SECONDS}"
done

echo "Timed out waiting for confidential CI to complete: ${details_url}" >&2
exit 1
