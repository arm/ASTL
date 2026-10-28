#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0
"""Generate deterministic C++ resources without running target executables."""
import argparse
import hashlib
import json
import posixpath
from pathlib import Path


def minify(text):
    # Remove only JSON whitespace outside strings; preserve numeric lexemes.
    result = []
    quoted = escaped = False
    for char in text:
        if quoted or char not in " \t\r\n":
            result.append(char)
        if escaped:
            escaped = False
        elif quoted and char == "\\":
            escaped = True
        elif char == '"':
            quoted = not quoted
    return "".join(result)


def json_files(root, ancestors=frozenset()):
    """Follow materialized overlay directories, rejecting symlink cycles."""
    real = root.resolve()
    if real in ancestors:
        raise ValueError(f"symlink cycle in config tree: {root}")
    for path in sorted(root.iterdir()):
        if path.is_dir():
            yield from json_files(path, ancestors | {real})
        elif path.suffix == '.json':
            yield path


def reject_nonstandard_constant(value):
    raise ValueError(f"non-standard JSON constant: {value}")


def generate(root, output, profile):
    entries = []
    documents = {}
    for path in sorted(json_files(root)):
        text = path.read_text(encoding="utf-8")
        documents[path.relative_to(root).as_posix()] = json.loads(text, parse_constant=reject_nonstandard_constant)
        payload = minify(text).encode("utf-8")
        entries.append((path.relative_to(root).as_posix(), payload))
    if not entries:
        raise ValueError("embedding input contains no JSON resources")
    # Check reference closure against the selected profile, not a larger source tree.
    for name, doc in documents.items():
        references = [doc["extends"]] if "extends" in doc else []
        for field, reference_key in (("scmi_uuid_mapping", "metrics_file"), ("uuid_mapping", "specification_file")):
            references += [entry[reference_key] for entry in doc.get(field, {}).values()]
        for reference in references:
            if not isinstance(reference, str) or reference.startswith('/'):
                raise ValueError(f"invalid embedded reference in {name}: {reference}")
            resolved = posixpath.normpath(posixpath.join(posixpath.dirname(name), reference))
            if resolved not in documents:
                raise ValueError(f"missing embedded reference in {name}: {reference}")
    lines = ['#include "config/embedded_config.hpp"', '#include <array>',
             'namespace astl::config { namespace {']
    for i, (_, data) in enumerate(entries):
        # Byte escapes are independent of the compiler's source character set.
        literal = ''.join('\\x%02x' % b for b in data)
        lines.append('constexpr char data_%d[] = "%s";' % (i, literal))
    lines.append('const std::array<EmbeddedResource, %d> resources = {{' % len(entries))
    for i, (name, data) in enumerate(entries):
        lines.append('{%s, std::string_view{data_%d, %d}},' % (json.dumps(name), i, len(data)))
    lines += ['}};', '}', 'auto EmbeddedResources() -> std::span<const EmbeddedResource> { return resources; }', '}']
    output.parent.mkdir(parents=True, exist_ok=True)
    content = '\n'.join(lines) + '\n'
    if not output.exists() or output.read_text() != content:
        output.write_text(content)
    manifest = {"profile": profile, "embedded": True, "resources": [
        {"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        for name, data in entries]}
    output.with_suffix('.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--profile', default='public')
    args = parser.parse_args()
    generate(args.root, args.output, args.profile)
