# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

"""Regression tests for ASTL's public and internal pre-commit configurations."""

import re
import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).parents[2]
PUBLIC_CONFIG = REPO_ROOT / ".pre-commit-config.yaml"
INTERNAL_CONFIG = REPO_ROOT / ".pre-commit-config-arm-debug.yaml"
COMMON_HOOK_IDS = [
    "format-staged",
    "qlty-check",
    "license-lint",
    "cppcheck",
    "cmake-lint",
    "add-pre-commit-trailer",
]


def _local_repository_block(config: Path) -> str:
    text = config.read_text(encoding="utf-8")
    start = text.index("  - repo: local\n")
    end = text.find("\n  - repo:", start + 1)
    block = text[start:] if end == -1 else text[start:end]
    return block.rstrip()


def _hook_ids(config: Path) -> list[str]:
    return re.findall(
        r"^      - id: (.+)$", config.read_text(encoding="utf-8"), re.MULTILINE
    )


def test_common_pre_commit_hooks_are_identical_and_ordered() -> None:
    """Internal-only providers must not cause the shared hooks to drift."""
    common_hooks = _local_repository_block(PUBLIC_CONFIG)
    assert common_hooks == _local_repository_block(INTERNAL_CONFIG)
    assert _hook_ids(PUBLIC_CONFIG) == COMMON_HOOK_IDS
    assert _hook_ids(INTERNAL_CONFIG)[: len(COMMON_HOOK_IDS)] == COMMON_HOOK_IDS
    assert "entry: ./scripts/format.sh --staged" in common_hooks
    assert "entry: qlty check --no-fix" in common_hooks
    assert "entry: ./scripts/license_lint.sh" in common_hooks
    assert "entry: ./scripts/cppcheck.sh build/debug" in common_hooks
    assert "entry: ./scripts/cmake_lint.sh" in common_hooks
    assert "files: '\\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$'" in common_hooks
    assert "fail_fast: true" in common_hooks
    assert common_hooks.count("stages: [pre-commit]") == len(COMMON_HOOK_IDS) - 1
    assert "entry: ./scripts/hooks/add_pre_commit_trailer.sh" in common_hooks
    assert common_hooks.count("stages: [commit-msg]") == 1


def test_public_config_cannot_fetch_or_run_ossmosis() -> None:
    """The public mirror's selected configuration has no ossmosis dependency."""
    text = PUBLIC_CONFIG.read_text(encoding="utf-8")
    assert "default_install_hook_types: [pre-commit, commit-msg]" in text
    assert "ossmosis" not in text.lower()


def test_internal_config_pins_both_ossmosis_stages() -> None:
    """The internal configuration pins the provider and installs both hook stages."""
    text = INTERNAL_CONFIG.read_text(encoding="utf-8")
    assert "default_install_hook_types: [pre-commit, commit-msg]" in text
    assert "repo: https://github.com/Arm-Debug/ossmosis" in text
    assert "rev: v0.2.0" in text
    assert _hook_ids(INTERNAL_CONFIG)[len(COMMON_HOOK_IDS) :] == [
        "ossmosis-check-commit",
        "ossmosis-check-message",
    ]


def test_commit_message_hook_adds_one_trailer(tmp_path: Path) -> None:
    """The message hook must add the adoption marker without duplicating it."""
    message = tmp_path / "COMMIT_EDITMSG"
    message.write_text("[NO-JIRA] Test message\n", encoding="utf-8")
    hook = REPO_ROOT / "scripts/hooks/add_pre_commit_trailer.sh"

    subprocess.run([hook, message], check=True)
    subprocess.run([hook, message], check=True)

    assert message.read_text(encoding="utf-8") == (
        "[NO-JIRA] Test message\n\nPre-Commit-Ran: true\n"
    )
