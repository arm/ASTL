# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def load_manifest_module():
    path = ROOT / "scripts" / "generate_release_manifest.py"
    spec = importlib.util.spec_from_file_location("generate_release_manifest", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_optional_release_and_source_metadata(tmp_path: Path) -> None:
    (tmp_path / "VERSION.md").write_text("1.2.3\n", encoding="utf-8")
    module = load_manifest_module()

    manifest = module.build_manifest(
        tmp_path,
        "1.2.3",
        "linux",
        "aarch64",
        "library_only",
        release_profile="test-profile",
        products=["test-product-a", "test-product-b"],
        astl_revision="a" * 40,
        astl_dirty=True,
        overlay_revision="b" * 40,
    )

    assert manifest["release"] == {
        "profile": "test-profile",
        "products": ["test-product-a", "test-product-b"],
    }
    assert manifest["sources"] == {
        "astl": {"revision": "a" * 40, "dirty": True},
        "overlay": {"revision": "b" * 40, "dirty": False},
    }


def test_optional_metadata_is_omitted_for_public_package(tmp_path: Path) -> None:
    module = load_manifest_module()
    manifest = module.build_manifest(tmp_path, "1.2.3", "linux", "aarch64", "library_only")

    assert "release" not in manifest
    assert "sources" not in manifest
