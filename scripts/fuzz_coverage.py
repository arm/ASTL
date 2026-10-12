#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0
"""Replay canonical corpora and report LLVM source-line coverage (ASTL-553)."""

import argparse
import hashlib
import os
import platform
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

from fuzz_coverage_report import SOURCE_SUFFIXES, write_reports

TARGETS = {
    "astl_wrapper_validation_fuzzer": "synthetic-wrapper",
    "astl_discovery_fuzzer": "procfs",
}


def load_exclusions(root):
    exclusions = {}
    for line in (root / "fuzz/coverage_exclusions.txt").read_text().splitlines():
        name, _, reason = line.partition("#")
        name, reason = name.strip(), reason.strip()
        if not name:
            continue
        path = Path(name)
        invalid_path = not name.startswith("src/") or ".." in path.parts
        if invalid_path or path.as_posix() != name:
            raise ValueError(f"Invalid fuzz coverage exclusion path: {line}")
        if not (root / path).is_file():
            raise ValueError(f"Missing fuzz coverage exclusion file: {name}")
        if not reason or name in exclusions:
            raise ValueError(
                f"Missing reason or duplicate fuzz coverage exclusion: {line}"
            )
        exclusions[name] = reason
    return exclusions


def source_record_lines(record, root, exclusions, files):
    path = (root / record[3:]).resolve()
    if not path.is_relative_to(root / "src"):
        return None
    name = path.relative_to(root).as_posix()
    if name in exclusions:
        return None
    return files.setdefault(name, {})


def merge_line_record(record, lines):
    number, count, *_ = record[3:].split(",")
    number, count = int(number), int(count)
    if number < 1 or count < 0:
        raise ValueError("Invalid LCOV line record")
    lines[number] = max(lines.get(number, 0), count)


def read_lcov(text, root, exclusions=()):
    """Union line hits by repository path; never average target percentages."""
    files = {}
    lines = None
    for record in text.splitlines():
        if record.startswith("SF:"):
            lines = source_record_lines(record, root, exclusions, files)
        elif record == "end_of_record":
            lines = None
        elif record.startswith("DA:") and lines is not None:
            merge_line_record(record, lines)
    if not any(files.values()):
        raise ValueError("LLVM report contains no executable lines under src/")
    return files


def export_lcov(tool, common):
    result = subprocess.run(
        [tool, "export", "-format=lcov", *common],
        text=True,
        capture_output=True,
        check=True,
    )
    if "mismatched data" in result.stderr:
        # Clang emits zero-hash placeholder mappings for unused inline functions.
        # A live definition elsewhere can make these placeholders warn. Only those
        # placeholders are safe to ignore; real hash mismatches invalidate coverage.
        diagnostics = subprocess.run(
            [tool, "export", "-dump", "-format=lcov", *common],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            check=True,
        ).stderr
        hashes = re.findall(r"with hash = 0x([0-9A-Fa-f]+)", diagnostics)
        if not hashes or any(int(value, 16) != 0 for value in hashes):
            raise ValueError(
                f"LLVM profile does not match coverage mappings:\n{diagnostics}"
            )
    return result.stdout, result.stderr


def prepare_corpus(root, target, evolved_root, corpus):
    """Copy seeds and optional evolved inputs into a deduplicated replay corpus."""
    manifest = {}
    directories = [("seed", root / "fuzz" / "corpus" / target)]
    if evolved_root:
        evolved = evolved_root.resolve() / target
        if not evolved.is_dir():
            raise ValueError(f"Missing evolved corpus directory: {evolved}")
        directories.append(("evolved", evolved))
    for origin, directory in directories:
        inputs = sorted(path for path in directory.rglob("*") if path.is_file())
        if not inputs:
            raise ValueError(f"Empty {origin} corpus: {directory}")
        for path in inputs:
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            shutil.copyfile(path, corpus / digest)
            manifest.setdefault(digest, []).append(origin)
    return manifest


def replay_targets(root, binaries, evolved_root, work, metadata):
    profiles = []
    for target, fixture in TARGETS.items():
        corpus = work / target
        corpus.mkdir()
        manifest = prepare_corpus(root, target, evolved_root, corpus)
        metadata["targets"][target] = {"fixture": fixture, "inputs": manifest}
        profile = work / f"{target}.profraw"
        env = dict(
            os.environ,
            LLVM_PROFILE_FILE=str(profile),
            ASTL_CONFIG_DIR=str(binaries / "config"),
            ASAN_OPTIONS="abort_on_error=1:detect_leaks=1:symbolize=1",
            UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
        )
        # Only discovery selects a profile; wrapper validation owns its synthetic fixture.
        env.pop("ASTL_FUZZ_FIXTURE_PROFILE", None)
        if target == "astl_discovery_fuzzer":
            env["ASTL_FUZZ_FIXTURE_PROFILE"] = fixture
        # -runs=0 executes the initial corpus without mutation. Do not inherit the .options time budget.
        subprocess.run(
            [
                str(binaries / target),
                str(corpus),
                "-runs=0",
                "-max_len=4096",
                "-timeout=10",
            ],
            cwd=binaries,
            env=env,
            check=True,
        )
        if not profile.is_file() or profile.stat().st_size == 0:
            raise ValueError(f"Target produced no LLVM profile: {target}")
        profiles.append(str(profile))
    return profiles


def run(args):
    root, output, binaries = (
        args.root.resolve(),
        args.output.resolve(),
        args.bin_dir.resolve(),
    )
    exclusions = load_exclusions(root)
    archive = binaries / "libastl_coverage.a"
    if not archive.is_file():
        raise ValueError(
            f"Missing coverage archive: {archive}; build with SANITIZER=coverage"
        )
    metadata = {
        "revision": args.revision,
        "compiler": (binaries / "compiler-version.txt").read_text().strip(),
        "llvm_cov": subprocess.check_output(
            [args.llvm_cov, "--version"], text=True
        ).strip(),
        "corpus_mode": "seeds",
        "targets": {},
        "build": {
            "platform": f"{platform.system().lower()}-{platform.machine()}",
            "procfs": True,
            "libsensors": False,
        },
    }
    with tempfile.TemporaryDirectory(prefix="astl-fuzz-coverage-") as temporary:
        work = Path(temporary)
        profiles = replay_targets(root, binaries, args.corpus, work, metadata)
        if args.corpus:
            metadata["corpus_mode"] = "seeds+evolved"
        merged = work / "coverage.profdata"
        subprocess.run(
            [args.llvm_profdata, "merge", "-sparse", *profiles, "-o", str(merged)],
            check=True,
        )
        sources = sorted(
            str(p) for p in (root / "src").rglob("*") if p.suffix in SOURCE_SUFFIXES
        )
        # The baseline includes unlinked objects and unused inline definitions.
        # Use a valid empty profile rather than -empty-profile, which LLVM 18 lacks.
        empty_text, empty_profile = work / "empty.txt", work / "empty.profdata"
        empty_text.write_text("astl-coverage-empty\n0\n1\n0\n")
        subprocess.run(
            [args.llvm_profdata, "merge", str(empty_text), "-o", str(empty_profile)],
            check=True,
        )
        baseline, baseline_diagnostics = export_lcov(
            args.llvm_cov, [str(archive), f"-instr-profile={empty_profile}", *sources]
        )
        binaries_args = [str(binaries / next(iter(TARGETS)))]
        binaries_args.extend(
            f"-object={binaries / target}" for target in list(TARGETS)[1:]
        )
        measured, diagnostics = export_lcov(
            args.llvm_cov, [*binaries_args, f"-instr-profile={merged}", *sources]
        )
        files = read_lcov(baseline + "\n" + measured, root, exclusions)
        metadata["llvm_diagnostics"] = baseline_diagnostics + diagnostics
        write_reports(files, root, output, metadata, exclusions)
        # Write the normalized union, not duplicate baseline/target LCOV records.
        records = []
        for name, lines in sorted(files.items()):
            records.append(f"SF:{name}")
            records.extend(
                f"DA:{number},{count}" for number, count in sorted(lines.items())
            )
            records.extend(
                [
                    f"LF:{len(lines)}",
                    f"LH:{sum(count > 0 for count in lines.values())}",
                    "end_of_record",
                ]
            )
        (output / "coverage.info").write_text("\n".join(records) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument("--bin-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument(
        "--corpus",
        type=Path,
        help="Evolved corpus root containing one directory per canonical target",
    )
    parser.add_argument("--llvm-cov", default="llvm-cov")
    parser.add_argument("--llvm-profdata", default="llvm-profdata")
    run(parser.parse_args())


if __name__ == "__main__":
    main()
