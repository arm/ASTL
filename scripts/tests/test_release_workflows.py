#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class ReleaseWorkflowContractTests(unittest.TestCase):
    def test_create_release_publishes_numbered_candidate_idempotently(self) -> None:
        workflow = (ROOT / ".github/workflows/create-release.yml").read_text(encoding="utf-8")

        self.assertIn("workflow_call:", workflow)
        self.assertIn("source_sha: ${{ steps.source-metadata.outputs.sha }}", workflow)
        self.assertIn("version: ${{ steps.version.outputs.version }}", workflow)
        self.assertIn("repository: Arm-Debug/ASTL", workflow)
        self.assertIn('CANDIDATE_TAG="${SOURCE_REF}"', workflow)
        self.assertIn("release-candidate/${VERSION_STRING}-rc\\.[1-9][0-9]*", workflow)
        self.assertIn("Reuse complete stable release", workflow)
        self.assertIn("steps.existing-release.outputs.reuse != 'true'", workflow)

    def test_candidate_workflow_creates_branch_and_numbered_tags(self) -> None:
        workflow = (ROOT / ".github/workflows/tag-release-candidate.yml").read_text(encoding="utf-8")

        self.assertIn("startsWith(github.event.pull_request.head.ref, 'cicd/prepare_release/')", workflow)
        self.assertIn('STABILIZATION_BRANCH="releases/${VERSION}"', workflow)
        self.assertIn('CANDIDATE_NUMBER=1', workflow)
        self.assertIn('CANDIDATE_TAG="release-candidate/${VERSION}-rc.${CANDIDATE_NUMBER}"', workflow)
        self.assertIn('"refs/tags/${CANDIDATE_TAG}:refs/tags/${CANDIDATE_TAG}"', workflow)

    def test_resolver_distinguishes_candidates_from_stable_releases(self) -> None:
        workflow = (ROOT / ".github/workflows/resolve-release.yml").read_text(encoding="utf-8")

        self.assertIn("CANDIDATE)", workflow)
        self.assertIn("STABLE)", workflow)
        self.assertIn("release-candidate/([0-9]+", workflow)
        self.assertIn("gh release view", workflow)
        self.assertIn("Public release ${SOURCE_REF} is missing ${asset}", workflow)

    def test_resolver_uses_caller_supplied_app_token(self) -> None:
        workflow = (ROOT / ".github/workflows/resolve-release.yml").read_text(encoding="utf-8")

        self.assertIn("APAP_APP_APP_ID:", workflow)
        self.assertIn("APAP_APP_PRIVATE_KEY:", workflow)
        self.assertIn("uses: actions/create-github-app-token@v3", workflow)
        self.assertIn("client-id: ${{ secrets.APAP_APP_APP_ID }}", workflow)
        self.assertIn("private-key: ${{ secrets.APAP_APP_PRIVATE_KEY }}", workflow)
        self.assertIn("token: ${{ steps.checkout-token.outputs.token }}", workflow)
        self.assertIn("GH_TOKEN: ${{ steps.checkout-token.outputs.token }}", workflow)

    def test_post_release_branch_starts_from_current_main(self) -> None:
        workflow = (ROOT / ".github/workflows/create-release.yml").read_text(encoding="utf-8")

        self.assertIn("post_release_version.py", workflow)
        self.assertIn('git switch -c "${POST_RELEASE_BRANCH}" origin/main', workflow)
        self.assertNotIn("is_reachable_from_main == 'true'", workflow)

    def test_create_release_tags_stable_go_module_idempotently(self) -> None:
        workflow = (ROOT / ".github/workflows/create-release.yml").read_text(encoding="utf-8")

        self.assertIn("name: 🏷️ Tag stable Go module", workflow)
        self.assertIn("if: env.RELEASE_TYPE == 'STABLE'", workflow)
        self.assertIn('EXPECTED_GO_MODULE="github.com/arm/ASTL/Go"', workflow)
        self.assertIn('EXPECTED_GO_MODULE="${EXPECTED_GO_MODULE}/v${GO_MAJOR}"', workflow)
        self.assertIn('grep -Fxq "module ${EXPECTED_GO_MODULE}" Go/go.mod', workflow)
        self.assertIn('GO_TAG="Go/v${VERSION_STRING}"', workflow)
        self.assertIn("git/matching-refs/tags/${GO_TAG}", workflow)
        self.assertIn("select(.ref == \\\"${GO_TAG_REF}\\\")", workflow)
        self.assertIn("git/tags/${GO_TAG_COMMIT}", workflow)
        self.assertIn('--method POST "repos/${ASTL_REPOSITORY}/git/refs"', workflow)
        self.assertIn('-f ref="${GO_TAG_REF}"', workflow)
        self.assertIn('-f sha="${ASTL_SOURCE_SHA}"', workflow)


if __name__ == "__main__":
    unittest.main()
