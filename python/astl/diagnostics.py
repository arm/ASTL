"""Diagnostics utilities for ASTL Python bindings.

Captures a lightweight snapshot of the host Python environment plus optional
ASTL runtime details (library semantic version and discovered target count).

The goal is to aid issue triage by making it easy for users to attach a JSON
blob to bug reports. All internal failures while gathering data are swallowed
so that diagnostics collection itself is effectively infallible.
"""
from __future__ import annotations

from dataclasses import dataclass, asdict
from typing import Any, Dict
import platform
import sys
import importlib
import os

try:
    from . import version, get_targets
except Exception:  # pragma: no cover - import failure path
    version = None  # type: ignore
    get_targets = None  # type: ignore


@dataclass
class Diagnostics:
    python_version: str
    platform: str
    implementation: str
    executable: str
    cwd: str
    astl_version: str | None
    target_count: int | None
    env_astl_config: str | None

    def to_dict(self) -> Dict[str, Any]:
        """Return a JSON-serializable dict representation."""
        return asdict(self)


def diagnostics(initialize_if_needed: bool = False) -> Diagnostics:
    """Collect a snapshot of runtime diagnostics.

    Parameters
    ----------
    initialize_if_needed:
        When True and the library may be uninitialized, perform a best-effort
        ``initialize(None)`` so that target enumeration can succeed. Errors are
        intentionally ignored—diagnostics collection must not raise.
    """
    astl_ver = None
    target_count = None
    if version is not None:
        try:
            v = version()
            if isinstance(v, (tuple, list)) and len(v) >= 4:
                astl_ver = v[3]
        except Exception:
            pass
    if get_targets is not None:
        try:
            if initialize_if_needed:
                from . import initialize  # local import to avoid cycle
                try:
                    initialize(None)
                except Exception:
                    pass
            t = get_targets()
            if isinstance(t, list):
                target_count = len(t)
        except Exception:
            pass

    return Diagnostics(
        python_version=sys.version.split()[0],
        platform=f"{platform.system()} {platform.release()}",
        implementation=platform.python_implementation(),
        executable=sys.executable,
        cwd=os.getcwd(),
        astl_version=astl_ver,
        target_count=target_count,
        env_astl_config=os.environ.get("ASTL_CONFIG"),
    )

__all__ = ["diagnostics", "Diagnostics"]

def diagnostics_cli():  # pragma: no cover - simple CLI interface
    """Console script entry point printing JSON diagnostics."""
    import json
    d = diagnostics().to_dict()
    print(json.dumps(d, indent=2, sort_keys=True))

