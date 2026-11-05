#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0
"""Keep published config references closed after explicit file exclusions."""

import argparse
import copy
import json
import posixpath
from pathlib import Path


def resolve_reference(owner: str, reference: str) -> str:
    if not isinstance(reference, str) or reference.startswith('/'):
        raise ValueError(f'invalid config reference in {owner}: {reference}')
    resolved = posixpath.normpath(posixpath.join(posixpath.dirname(owner), reference))
    if resolved == '..' or resolved.startswith('../'):
        raise ValueError(f'config reference escapes published tree in {owner}: {reference}')
    return resolved


def exclude_inheriting_documents(documents: dict, excluded: set[str]) -> None:
    # A declaration cannot be published without its base. Propagate exclusions
    # before processing mappings, including chains of inheritance.
    while True:
        removed = {name for name, doc in documents.items()
                   if 'extends' in doc and resolve_reference(name, doc['extends']) in excluded}
        if not removed:
            break
        excluded.update(removed)
        for name in removed:
            del documents[name]


def prune_mapping(owner: str, mapping: dict, documents: dict, excluded: set[str]) -> None:
    for key, entry in list(mapping.items()):
        references = [entry[key] for key in ('metrics_file', 'specification_file') if key in entry]
        if any(resolve_reference(owner, reference) in excluded for reference in references):
            del mapping[key]
        else:
            for reference in references:
                validate_reference(owner, reference, documents)


def prune_references(documents: dict, excluded: set[str]) -> dict:
    """Remove only dependencies of explicitly excluded files; reject other missing references."""
    documents = copy.deepcopy(documents)
    # Release replacements may deliberately restore a filtered path with public content.
    excluded = set(excluded) - documents.keys()
    exclude_inheriting_documents(documents, excluded)
    for name, doc in documents.items():
        if 'extends' in doc:
            validate_reference(name, doc['extends'], documents)
        for field in ('scmi_uuid_mapping', 'uuid_mapping'):
            prune_mapping(name, doc.get(field, {}), documents, excluded)
    return documents


def validate_reference(owner: str, reference: str, documents: dict) -> None:
    if resolve_reference(owner, reference) not in documents:
        raise ValueError(f'missing published config reference in {owner}: {reference}')


def prune_tree(root: Path, excluded: set[str]) -> None:
    original = {path.relative_to(root).as_posix(): json.loads(path.read_text(encoding='utf-8'))
                for path in sorted(root.rglob('*.json'))}
    published = prune_references(original, excluded)
    # Validate everything before modifying the published tree. Leave unchanged
    # documents byte-for-byte intact, including confidential-mode copies.
    for name, doc in original.items():
        path = root / name
        if name not in published:
            path.unlink()
        elif published[name] != doc:
            path.write_text(json.dumps(published[name], indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--excluded', type=Path, required=True)
    args = parser.parse_args()
    prune_tree(args.root, set(args.excluded.read_text(encoding='utf-8').splitlines()))
