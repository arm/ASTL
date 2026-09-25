#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Verify that coverage shards and isolated CTest entries cover each Catch2 case once."""

import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET


def list_cases(command: list[str], build_dir: Path) -> Counter[str]:
    """Ask the actual binary to apply CTest's filters and shard partition."""
    args = command.copy()
    if "--reporter" in args:
        index = args.index("--reporter")
        del args[index : index + 2]
    output = subprocess.check_output(
        [*args, "--list-tests", "--reporter", "xml"], cwd=build_dir, text=True
    )
    return Counter(case.findtext("Name", "") for case in ET.fromstring(output).findall("TestCase"))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()
    build_dir = args.build_dir.resolve()
    tests = json.loads(
        subprocess.check_output(
            ["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"], text=True
        )
    )["tests"]
    binaries = {
        test["command"][0]
        for test in tests
        if any(
            prop["name"] == "LABELS" and "coverage" in prop["value"]
            for prop in test.get("properties", [])
        )
    }
    if not binaries:
        raise SystemExit("No coverage shards registered")
    for binary in sorted(binaries):
        expected = list_cases([binary], build_dir)
        actual: Counter[str] = Counter()
        for test in tests:
            command = test.get("command", [])
            if command and command[0] == binary:
                actual.update(list_cases(command, build_dir))
        if actual != expected:
            raise SystemExit(
                f"{binary}: missing cases {dict(expected - actual)}; "
                f"extra or repeated cases {dict(actual - expected)}"
            )
        print(f"{Path(binary).name}: all {sum(expected.values())} cases registered exactly once")


if __name__ == "__main__":
    main()
