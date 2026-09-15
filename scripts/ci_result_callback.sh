#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0
# shellcheck disable=SC2153  # Environment names are validated indirectly below.

set -euo pipefail
required=(GH_TOKEN TARGET_REPOSITORY CALLBACK_EVENT SCHEMA_VERSION CORRELATION_ID HEAD_SHA CHECK_RUN_ID CHECK_NAME CONCLUSION DETAILS_URL)
for name in "${required[@]}"; do
	[[ -n ${!name:-} ]] || {
		echo "Required callback variable is empty: ${name}" >&2
		exit 2
	}
done
[[ ${TARGET_REPOSITORY} == "Arm-Debug/ASTL" ]] || {
	echo "Unsupported callback target." >&2
	exit 2
}
[[ ${SCHEMA_VERSION} == "2" ]] || {
	echo "Unsupported callback schema." >&2
	exit 2
}
[[ ${CORRELATION_ID} =~ ^[A-Za-z0-9_.-]+$ ]] || {
	echo "Invalid correlation ID." >&2
	exit 2
}
[[ ${HEAD_SHA} =~ ^[0-9a-fA-F]{40}$ ]] || {
	echo "Invalid callback SHA." >&2
	exit 2
}
[[ ${CHECK_RUN_ID} =~ ^[1-9][0-9]*$ ]] || {
	echo "Invalid check run ID." >&2
	exit 2
}
[[ ${DETAILS_URL} =~ ^https://github.com/Arm-Debug/ASTL-confidential/actions/runs/[1-9][0-9]*$ ]] || {
	echo "Invalid private details URL." >&2
	exit 2
}
case "${CALLBACK_EVENT}:${CHECK_NAME}" in
confidential-ci-result:Confidential\ CI | amx-ci-result:AMX\ CI\ /\ aggregate) ;;
*)
	echo "Callback event and check name do not match." >&2
	exit 2
	;;
esac
[[ ${CONCLUSION} =~ ^(success|failure|cancelled)$ ]] || {
	echo "Invalid conclusion." >&2
	exit 2
}

check="$(gh api -H "Accept: application/vnd.github+json" "repos/${TARGET_REPOSITORY}/check-runs/${CHECK_RUN_ID}")"
[[ $(jq -r '.name' <<<"${check}") == "${CHECK_NAME}" ]] || {
	echo "Check name mismatch." >&2
	exit 2
}
head_sha="$(tr 'A-F' 'a-f' <<<"${HEAD_SHA}")"
[[ $(jq -r '.head_sha' <<<"${check}") == "${head_sha}" ]] || {
	echo "Check SHA mismatch." >&2
	exit 2
}
[[ $(jq -r '.external_id' <<<"${check}") == "${CORRELATION_ID}" ]] || {
	echo "Check correlation mismatch." >&2
	exit 2
}

gh api --method PATCH -H "Accept: application/vnd.github+json" \
	"repos/${TARGET_REPOSITORY}/check-runs/${CHECK_RUN_ID}" \
	-f status=completed -f conclusion="${CONCLUSION}" -f details_url="${DETAILS_URL}" \
	-f "output[title]=${CHECK_NAME}: ${CONCLUSION}" \
	-f "output[summary]=ASTL-confidential reported ${CONCLUSION} for correlation ${CORRELATION_ID}. [View the private workflow run](${DETAILS_URL})." >/dev/null
echo "Completed ${CHECK_NAME} check ${CHECK_RUN_ID} with ${CONCLUSION}."
