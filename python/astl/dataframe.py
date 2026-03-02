# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Optional pandas integration helpers.

``to_dataframe`` converts a mapping of ``entity_name -> list[(timestamp, value)]``
into a tidy/long-form DataFrame (columns: name, timestamp, value) when the
``pandas`` package is available.

Behavior:
    * If pandas import fails and ``silent=False`` (default) an ``ImportError`` is raised.
    * If ``silent=True`` the function returns ``None`` instead, enabling graceful
        optional dependency patterns.

Example::

        samples = {"cpu_cycles": [(1, 10), (2, 15)], "energy_j": [(1, 1.2)]}
        df = to_dataframe(samples)
        # df:
        # name       timestamp  value
        # cpu_cycles 1          10
        # cpu_cycles 2          15
        # energy_j   1          1.2
"""
from __future__ import annotations

from typing import Dict, List, Tuple, Any


def to_dataframe(samples: Dict[str, List[Tuple[int, object]]], *, silent: bool = False):
    try:
        import pandas as pd  # type: ignore
    except ImportError:  # Only catch actual import failures, not unrelated runtime issues
        if silent:
            return None
        raise ImportError("pandas is required for to_dataframe(); install pandas or pass silent=True")
    records = []  # list[dict[str, object]] rows for DataFrame construction
    for name, sample_seq in samples.items():
        for timestamp, value in sample_seq:
            records.append({"name": name, "timestamp": timestamp, "value": value})
    # For very large sample sets, consider constructing column lists and dict directly
    # to avoid per-row dict overhead. Current approach favors clarity.
    return pd.DataFrame(records, columns=["name", "timestamp", "value"])  # type: ignore

__all__ = ["to_dataframe"]
