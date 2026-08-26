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

    def test_post_release_branch_starts_from_current_main(self) -> None:
        workflow = (ROOT / ".github/workflows/create-release.yml").read_text(encoding="utf-8")

        self.assertIn("post_release_version.py", workflow)
        self.assertIn('git switch -c "${POST_RELEASE_BRANCH}" origin/main', workflow)
        self.assertNotIn("is_reachable_from_main == 'true'", workflow)


if __name__ == "__main__":
    unittest.main()
