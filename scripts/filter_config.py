#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0
"""Detach a prepared config tree and optionally restrict it to a release-profile allowlist."""
import argparse
import os
import shutil
import tempfile
from pathlib import Path, PurePosixPath


def materialize(source, destination, ancestors=frozenset()):
    """Copy overlay contents without retaining links or traversing cycles."""
    real = source.resolve()
    if real in ancestors:
        raise ValueError(f'symlink cycle in config tree: {source}')
    destination.mkdir()
    for entry in sorted(source.iterdir()):
        target = destination / entry.name
        if entry.is_dir():
            materialize(entry, target, ancestors | {real})
        else:
            shutil.copy2(entry, target)


def filter_config(root, includes=()):
    paths = [PurePosixPath(value) for value in includes]
    if any(p.is_absolute() or '..' in p.parts or str(p) == '.' for p in paths):
        raise ValueError('config includes must be nonempty relative paths without parent traversal')
    # Never prune through overlay links: prepare and validate a detached copy,
    # then replace the input tree. Failures before replacement leave it intact.
    root = Path(os.path.abspath(root))
    with tempfile.TemporaryDirectory(prefix='.astl-filter-', dir=root.parent) as temporary:
        stage = Path(temporary) / 'config'
        materialize(root, stage)
        files = sorted(stage.rglob('*.json'))
        selected = set() if paths else set(files)
        for prefix in paths:
            matches = {p for p in files if PurePosixPath(p.relative_to(stage).as_posix()).is_relative_to(prefix)}
            if not matches:
                raise ValueError(f'config include matches no JSON: {prefix}')
            selected.update(matches)
        for path in files:
            if path not in selected:
                path.unlink()
        backup = Path(temporary) / 'original'
        root.rename(backup)
        try:
            stage.rename(root)
        except OSError:
            backup.rename(root)
            raise
        # TemporaryDirectory removes the old tree without following its symlinks.


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('includes', nargs='*', help='optional allowlist; omitted keeps all files')
    args = parser.parse_args()
    filter_config(args.root, args.includes)
