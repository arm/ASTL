#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class ReleaseWorkflowContractTests(unittest.TestCase):
    def test_create_release_exposes_reusable_candidate_contract(self) -> None:
        workflow = (ROOT / ".github/workflows/create-release.yml").read_text(encoding="utf-8")

        self.assertIn("workflow_call:", workflow)
        self.assertIn("source_sha: ${{ steps.source-metadata.outputs.sha }}", workflow)
        self.assertIn("version: ${{ steps.version.outputs.version }}", workflow)
        self.assertIn("repository: Arm-Debug/ASTL", workflow)
        self.assertIn('CANDIDATE_TAG="release-candidate/${VERSION_STRING}"', workflow)

    def test_merged_preparation_pr_creates_candidate_tag(self) -> None:
        workflow = (ROOT / ".github/workflows/tag-release-candidate.yml").read_text(encoding="utf-8")

        self.assertIn("startsWith(github.event.pull_request.head.ref, 'cicd/prepare_release/')", workflow)
        self.assertIn('CANDIDATE_TAG="release-candidate/${VERSION}"', workflow)
        self.assertIn('git push origin "refs/tags/${CANDIDATE_TAG}"', workflow)


if __name__ == "__main__":
    unittest.main()
