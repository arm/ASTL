# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise real static/shared module discovery from an unrelated working directory."""
import os
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

probe, library = sys.argv[1:]


def write_valid_config(directory):
    # Windows registers built-in CPU/memory metrics even without metric JSON.
    # Their group declarations are required for successful API initialization.
    groups = directory / "groups"
    groups.mkdir()
    (groups / "metric_groups.json").write_text(json.dumps({
        "metric_groups": {name: {"description": name} for name in ("cpu", "memory")}
    }))


with tempfile.TemporaryDirectory(prefix="astl-config-location-") as temporary:
    root = Path(temporary)
    executable = root / "bin" / Path(probe).name
    executable.parent.mkdir()
    shutil.copy2(probe, executable)
    if sys.platform == "win32":
        # vcpkg deploys dependency DLLs beside each build-tree executable.
        # Keep ASTL itself in the separate library directory for the shared probe.
        for dependency in Path(probe).parent.glob("*.dll"):
            if dependency.name.casefold() != Path(library).name.casefold():
                shutil.copy2(dependency, executable.parent / dependency.name)
    adjacent = executable.parent
    env = dict(os.environ)
    env.pop("ASTL_CONFIG_DIR", None)
    env.pop("ASTL_COLLECTORS", None)
    if library != "static":
        adjacent = root / "lib"
        adjacent.mkdir()
        shutil.copy2(library, adjacent / Path(library).name)
        if sys.platform == "win32":
            env["PATH"] = str(adjacent) + os.pathsep + env.get("PATH", "")
        elif sys.platform == "darwin":
            env["DYLD_LIBRARY_PATH"] = str(adjacent)
        else:
            env["LD_LIBRARY_PATH"] = str(adjacent)
    # A config directory in the working directory must not affect discovery.
    (root / "config").mkdir()
    (root / "config" / "bad.json").write_text("{")

    changed_cwd = root / "changed"
    changed_cwd.mkdir()

    def check(expected):
        subprocess.run([str(executable), expected], cwd=root, env=env, check=True)
        # Launch via a relative path and chdir before the first ASTL call.
        # Shared-library paths must also survive a relative loader search path.
        relative_env = dict(env)
        if library != "static" and sys.platform != "win32":
            key = "DYLD_LIBRARY_PATH" if sys.platform == "darwin" else "LD_LIBRARY_PATH"
            relative_env[key] = "lib"
        # Windows resolves a relative executable against the parent's cwd,
        # not subprocess.run's cwd argument. Set the launch directory explicitly.
        previous_cwd = Path.cwd()
        try:
            os.chdir(root)
            subprocess.run([str(executable.relative_to(root)), expected, str(changed_cwd)],
                           env=relative_env, check=True)
        finally:
            os.chdir(previous_cwd)

    check("good")
    config = adjacent / "config"
    config.mkdir()
    (config / "bad.json").write_text("{")
    check("bad")
    explicit = root / "explicit"
    explicit.mkdir()
    write_valid_config(explicit)
    env["ASTL_CONFIG_DIR"] = str(explicit)
    check("good")
    env["ASTL_CONFIG_DIR"] = str(root / "missing")
    check("bad")
    env.pop("ASTL_CONFIG_DIR")
    (config / "bad.json").unlink()
    write_valid_config(config)
    check("good")
