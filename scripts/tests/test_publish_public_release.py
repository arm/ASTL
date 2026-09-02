#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PUBLISHER = ROOT / "scripts/release/publish_public_release.sh"


class PublishPublicReleaseTests(unittest.TestCase):
    def test_create_and_retry(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            log = temporary / "gh.log"
            gh = temporary / "gh"
            gh.write_text(
                """#!/usr/bin/env bash
set -eu
printf '%s|%s\\n' "${GH_TOKEN}" "$*" >>"${GH_LOG}"
if [[ "${GH_TOKEN}:$1:$2" == "source:release:view" ]]; then
  printf '%s' '{"name":"ASTL 1.2.3","body":"notes","isDraft":false,"isPrerelease":false,"assets":[{"name":"release-complete.json"}]}'
elif [[ "${GH_TOKEN}:$1:$2" == "source:release:download" ]]; then
  while (($#)); do
    if [[ "$1" == "--dir" ]]; then touch "$2/release-complete.json"; break; fi
    shift
  done
elif [[ "${GH_TOKEN}:$1:$2" == "target:release:view" ]]; then
  exit "${TARGET_EXISTS}"
fi
""",
                encoding="utf-8",
            )
            gh.chmod(0o755)
            environment = {
                **os.environ,
                "PATH": f"{temporary}:{os.environ['PATH']}",
                "GH_LOG": str(log),
                "SOURCE_GITHUB_TOKEN": "source",
                "TARGET_GITHUB_TOKEN": "target",
            }

            for target_exists, expected in (("1", "create"), ("0", "edit")):
                subprocess.run(
                    [PUBLISHER, "release/1.2.3"],
                    check=True,
                    env={**environment, "TARGET_EXISTS": target_exists},
                )
                calls = log.read_text(encoding="utf-8")
                self.assertIn(f"target|release {expected} release/1.2.3", calls)
                self.assertIn("target|release upload release/1.2.3", calls)
                self.assertIn("--clobber", calls)
                log.write_text("", encoding="utf-8")

    def test_rejects_non_stable_tag(self) -> None:
        result = subprocess.run(
            [PUBLISHER, "release/rolling"], capture_output=True, text=True
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Not a stable release tag", result.stderr)


if __name__ == "__main__":
    unittest.main()
