#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

"""Publish coverage for the exact public main commit, without exposing the full report."""

import json
import os
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path

SOURCE = "Arm-Debug/ASTL"
TARGET = "arm/ASTL"


def api(endpoint, token, payload=None, method="GET"):
    result = subprocess.run(
        ["gh", "api", endpoint, "--method", method] + (["--input", "-"] if payload is not None else []),
        input=json.dumps(payload) if payload is not None else None,
        text=True,
        capture_output=True,
        env={**os.environ, "GH_TOKEN": token},
        check=True,
    )
    return json.loads(result.stdout)


def render_badge(report):
    root = ET.parse(report).getroot()
    covered = int(root.attrib["lines-covered"])
    valid = int(root.attrib["lines-valid"])
    if valid <= 0 or not 0 <= covered <= valid:
        raise ValueError("Invalid coverage line counts")
    percentage = covered * 100 / valid
    message = f"{percentage:.0f}%"
    color = "#4c1" if percentage >= 80 else "#dfb317" if percentage >= 60 else "#e05d44"
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="154" height="20" role="img" aria-label="test coverage: {message}">
<title>test coverage: {message}</title>
<linearGradient id="s" x2="0" y2="100%"><stop offset="0" stop-color="#bbb" stop-opacity=".1"/><stop offset="1" stop-opacity=".1"/></linearGradient>
<clipPath id="r"><rect width="154" height="20" rx="3"/></clipPath>
<g clip-path="url(#r)"><path fill="#555" d="M0 0h112v20H0z"/><path fill="{color}" d="M112 0h42v20h-42z"/><path fill="url(#s)" d="M0 0h154v20H0z"/></g>
<g fill="#fff" text-anchor="middle" font-family="Verdana,Geneva,DejaVu Sans,sans-serif" font-size="11">
<text x="56" y="15" fill="#010101" fill-opacity=".3">test coverage</text><text x="56" y="14">test coverage</text>
<text x="133" y="15" fill="#010101" fill-opacity=".3">{message}</text><text x="133" y="14">{message}</text></g></svg>
'''
    return svg, percentage


def main():
    source_token = os.environ["GH_TOKEN"]
    public_token = os.environ["PUBLIC_GITHUB_TOKEN"]
    public_ref = f"repos/{TARGET}/git/ref/heads/main"
    sha = api(public_ref, public_token)["object"]["sha"]
    runs = api(
        f"repos/{SOURCE}/actions/workflows/integration.yml/runs?branch=main&head_sha={sha}&status=success&per_page=100",
        source_token,
    )["workflow_runs"]
    # API lists newest runs first. Never select PR coverage, even for the same SHA.
    run = next((r for r in runs if r["event"] in ("push", "schedule", "workflow_dispatch")), None)
    if run is None:
        print("No successful coverage run for public main; keeping the previous badge.")
        return
    artifacts = api(f"repos/{SOURCE}/actions/runs/{run['id']}/artifacts", source_token)["artifacts"]
    if not any(a["name"] == "public-coverage-report" and not a["expired"] for a in artifacts):
        print("Coverage artifact unavailable; keeping the previous badge.")
        return
    with tempfile.TemporaryDirectory() as directory:
        subprocess.run(
            ["gh", "run", "download", str(run["id"]), "--repo", SOURCE,
             "--name", "public-coverage-report", "--dir", directory], check=True,
        )
        svg, percentage = render_badge(Path(directory) / "coverage.xml")
    metadata = json.dumps({"commit": sha, "coverage_percent": percentage,
                           "measured_at": run["updated_at"],
                           "published_at": datetime.now(timezone.utc).isoformat()}, indent=2) + "\n"
    badge_ref = f"repos/{TARGET}/git/ref/heads/badges"
    try:
        parent = api(badge_ref, public_token)["object"]["sha"]
    except subprocess.CalledProcessError as error:
        if "HTTP 404" not in error.stderr:
            raise
        parent = None
    tree_payload = {"tree": [{"path": name, "mode": "100644", "type": "blob", "content": content}
                             for name, content in (("coverage.svg", svg), ("coverage.json", metadata))]}
    if parent:
        tree_payload["base_tree"] = api(f"repos/{TARGET}/git/commits/{parent}", public_token)["tree"]["sha"]
    tree = api(f"repos/{TARGET}/git/trees", public_token, tree_payload, "POST")
    commit = api(f"repos/{TARGET}/git/commits", public_token,
                 {"message": f"Update coverage badge for {sha}", "tree": tree["sha"],
                  "parents": [parent] if parent else []}, "POST")
    # Mirroring can race this workflow. Do not publish a result for an obsolete main.
    if api(public_ref, public_token)["object"]["sha"] != sha:
        print("Public main changed during publication; retry on the next trigger.")
        return
    if parent:
        api(f"repos/{TARGET}/git/refs/heads/badges", public_token,
            {"sha": commit["sha"], "force": False}, "PATCH")
    else:
        api(f"repos/{TARGET}/git/refs", public_token,
            {"ref": "refs/heads/badges", "sha": commit["sha"]}, "POST")
    print(f"Published {percentage:.1f}% coverage for public main {sha}.")


if __name__ == "__main__":
    main()
