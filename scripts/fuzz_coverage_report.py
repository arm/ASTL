#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0
"""Render canonical fuzz coverage reports from normalized source-line counts."""

import hashlib
import html
import json
import xml.etree.ElementTree as ET

SOURCE_SUFFIXES = {".hpp", ".c", ".hh", ".hxx", ".h", ".cc", ".cxx", ".cpp"}


def write_html(files, root, output):
    """Render the same union used by XML/JSON, including the zero-count baseline."""
    directory = output / "html"
    directory.mkdir(parents=True, exist_ok=True)
    style = (
        "body{font:15px system-ui;margin:2rem}table{border-collapse:collapse}"
        "td,th{padding:2px 12px;text-align:left}pre{margin:0;white-space:pre-wrap}"
        ".covered{background:#dcfce7}.uncovered{background:#fee2e2}"
        ".count{color:#555;text-align:right}a{color:#075985}"
    )

    def page(title, body):
        return (
            f'<!doctype html><html lang="en"><meta charset="utf-8">'
            f"<title>{html.escape(title)}</title><style>{style}</style>"
            f"<h1>{html.escape(title)}</h1>{body}</html>"
        )

    rows = []
    total = sum(len(lines) for lines in files.values())
    covered = sum(count > 0 for lines in files.values() for count in lines.values())
    for index, (name, lines) in enumerate(sorted(files.items())):
        filename = f"source-{index}.html"
        count = sum(value > 0 for value in lines.values())
        rows.append(
            f'<tr><td><a href="{filename}">{html.escape(name)}</a></td>'
            f"<td>{count} / {len(lines)}</td></tr>"
        )
        source_rows = []
        for number, source in enumerate((root / name).read_text().splitlines(), 1):
            hits = lines.get(number)
            status = "" if hits is None else ("covered" if hits else "uncovered")
            source_rows.append(
                f'<tr id="L{number}" class="{status}">'
                f'<td><a href="#L{number}">{number}</a></td>'
                f'<td class="count">{hits if hits is not None else ""}</td>'
                f"<td><pre>{html.escape(source)}</pre></td></tr>"
            )
        body = (
            '<p><a href="index.html">All files</a> · Green: covered; red: uncovered; '
            "unshaded: no executable-line mapping. Counts do not measure branch coverage.</p>"
            "<table><thead><tr><th>Line</th><th>Hits</th><th>Source</th></tr></thead><tbody>"
            + "".join(source_rows)
            + "</tbody></table>"
        )
        (directory / filename).write_text(page(name, body))
    body = (
        f"<p>{covered:,} / {total:,} executable lines under src/. "
        'Canonical corpus replay; see <a href="../summary.md">summary</a> and '
        '<a href="../uncovered.json">uncovered JSON</a> for scope and provenance.</p>'
        "<table><thead><tr><th>File</th><th>Covered / executable lines</th></tr></thead><tbody>"
        + "".join(rows)
        + "</tbody></table>"
    )
    (directory / "index.html").write_text(
        page(f"Fuzz source-line coverage: {100 * covered / total:.2f}%", body)
    )


def write_badge(rate, path):
    """Compact, self-contained SVG matching the README's other status badges."""
    color = (
        "#4c1"
        if rate >= 0.9
        else "#97ca00"
        if rate >= 0.75
        else "#dfb317"
        if rate >= 0.5
        else "#e05d44"
    )
    path.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" width="136" height="20" role="img" '
        f'aria-label="fuzz coverage: {100 * rate:.1f}%">'
        f"<title>Fuzz source-line coverage: {100 * rate:.1f}%</title>"
        '<linearGradient id="s" x2="0" y2="100%"><stop offset="0" stop-color="#bbb" stop-opacity=".1"/>'
        '<stop offset="1" stop-opacity=".1"/></linearGradient>'
        '<clipPath id="r"><rect width="136" height="20" rx="3"/></clipPath>'
        '<g clip-path="url(#r)"><path fill="#555" d="M0 0h90v20H0z"/>'
        f'<path fill="{color}" d="M90 0h46v20H90z"/>'
        '<path fill="url(#s)" d="M0 0h136v20H0z"/></g>'
        '<g fill="#fff" text-anchor="middle" font-family="Verdana,Geneva,DejaVu Sans,sans-serif" font-size="11">'
        '<text x="45" y="15" fill="#010101" fill-opacity=".3">fuzz coverage</text>'
        '<text x="45" y="14">fuzz coverage</text>'
        f'<text x="113" y="15" fill="#010101" fill-opacity=".3">{100 * rate:.1f}%</text>'
        f'<text x="113" y="14">{100 * rate:.1f}%</text></g></svg>\n'
    )


def write_reports(files, root, output, metadata, exclusions=None):
    total = sum(len(lines) for lines in files.values())
    covered = sum(count > 0 for lines in files.values() for count in lines.values())
    rate = covered / total
    summary = {"covered": covered, "total": total, "percent": round(100 * rate, 2)}
    report = {
        "schema_version": 1,
        "scope": "LLVM executable lines under src/ in the canonical Linux build, including headers and unlinked objects",
        "metadata": metadata,
        "summary": summary,
        "files": [],
        "unmeasured_files": [],
        "excluded_files": exclusions or {},
    }
    coverage = ET.Element(
        "coverage",
        {
            "line-rate": str(rate),
            "lines-covered": str(covered),
            "lines-valid": str(total),
            "branch-rate": "0",
            "branches-covered": "0",
            "branches-valid": "0",
            "complexity": "0",
            "version": "astl-fuzz-1",
            "timestamp": "0",
        },
    )
    ET.SubElement(ET.SubElement(coverage, "sources"), "source").text = "."
    package = ET.SubElement(
        ET.SubElement(coverage, "packages"),
        "package",
        {
            "name": "src",
            "line-rate": str(rate),
            "branch-rate": "0",
            "complexity": "0",
        },
    )
    classes = ET.SubElement(package, "classes")
    for name, lines in sorted(files.items()):
        source = (root / name).read_text().splitlines()
        if any(number > len(source) for number in lines):
            raise ValueError(f"Coverage does not match source: {name}")
        hits = sum(count > 0 for count in lines.values())
        report["files"].append(
            {
                "path": name,
                "source_sha256": hashlib.sha256((root / name).read_bytes()).hexdigest(),
                "covered": hits,
                "total": len(lines),
                "uncovered_lines": [
                    {"line": number, "source": source[number - 1]}
                    for number, count in sorted(lines.items())
                    if count == 0
                ],
            }
        )
        cls = ET.SubElement(
            classes,
            "class",
            {
                "name": name,
                "filename": name,
                "line-rate": str(hits / len(lines) if lines else 0),
                "branch-rate": "0",
                "complexity": "0",
            },
        )
        ET.SubElement(cls, "methods")
        xml_lines = ET.SubElement(cls, "lines")
        for number, count in sorted(lines.items()):
            ET.SubElement(
                xml_lines,
                "line",
                {"number": str(number), "hits": str(count), "branch": "false"},
            )
    report["unmeasured_files"] = sorted(
        path.relative_to(root).as_posix()
        for path in (root / "src").rglob("*")
        if path.suffix in SOURCE_SUFFIXES
        and path.relative_to(root).as_posix() not in files
        and path.relative_to(root).as_posix() not in (exclusions or {})
    )
    output.mkdir(parents=True, exist_ok=True)
    ET.indent(coverage)
    ET.ElementTree(coverage).write(
        output / "coverage.xml", encoding="utf-8", xml_declaration=True
    )
    (output / "uncovered.json").write_text(json.dumps(report, indent=2) + "\n")
    write_badge(rate, output / "badge.svg")
    text = (
        f"# Fuzz source-line coverage: {100 * rate:.2f}%\n\n"
        f"{covered:,} / {total:,} executable lines under `src/`. "
        "Combined canonical corpus replay; excludes tests, harnesses, dependencies and host-dependent profiles.\n\n"
        f"Revision: `{metadata['revision']}`. Corpus: **{metadata['corpus_mode']}**.\n\n"
        "Download the `fuzz-coverage` artifact: open `html/index.html` for annotated source, "
        "or give `uncovered.json` to a harness-generation tool. `coverage.xml` is an artifact-only Cobertura report; "
        "fuzz coverage is not uploaded to GitHub Code Quality or included in the test coverage merge threshold.\n\n"
        f"{len(report['unmeasured_files'])} source/header files have no coverage mapping; "
        "see `unmeasured_files` in JSON. They are not silently treated as covered or counted as executable lines.\n\n"
        f"{len(exclusions or {})} files explicitly excluded by `fuzz/coverage_exclusions.txt`; "
        "see `excluded_files` in JSON for paths and reasons.\n\n"
        "| File | Covered / executable lines |\n| --- | ---: |\n"
    )
    for file in sorted(
        report["files"], key=lambda f: (-(f["total"] - f["covered"]), f["path"])
    ):
        text += f"| `{file['path']}` | {file['covered']} / {file['total']} |\n"
    (output / "summary.md").write_text(text)
    write_html(files, root, output)
    return report
