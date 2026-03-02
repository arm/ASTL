# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Session abstraction for ASTL collections.

The :class:`Session` class encapsulates a *minimal* convenient pattern for
configuring a set of counters / metrics on a single target and performing one
or more immediate polls inside a ``with`` block. It intentionally aims to be
unopinionated and light—power users can directly call the lower level APIs.

Responsibilities handled:
    * Optional library initialization (``auto_initialize=True``).
    * Configuration of counters / metrics with an IMMEDIATE collection mode.
    * Best-effort lifecycle management: ``start_collection`` on enter and
        ``stop_collection`` on exit. Failures (e.g., NOT_IMPLEMENTED) are swallowed
        to keep the helper resilient across partial backend implementations.
    * A ``poll_once`` helper that issues a ``read_immediate`` then retrieves all
        available samples for each configured entity, returning a nested dict::

                {
                        "counters": {"counter_name": [(ts, value), ...]},
                        "metrics":  {"metric_name":  [(ts, value), ...]},
                }

Usage example::

        from astl import Session, get_targets, get_counters
        t = get_targets()[0]
        counters = get_counters(t)[:2]
        with Session(target=t, counters=counters, interval_us=1000, auto_initialize=True) as sess:
                snap = sess.poll_once()
                print(snap["counters"])  # mapping name -> samples list

Limitations / future directions:
    * Only IMMEDIATE mode is exposed right now; background sampling parameters
        can be surfaced later without breaking the interface (additional kwargs).
    * No internal retry/backoff or sample deduplication—the returned lists are
        exactly what the underlying API provides at call time.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Sequence, Any, Dict, List, Tuple
import time

from . import (
    configure_counters_on_target,
    configure_metrics_on_target,
    start_collection,
    stop_collection,
    read_immediate,
    get_counter_samples,
    get_metric_samples,
    CollectionParameters,
    CollectionMode,
    Target,
)


@dataclass
class Session:
    target: Target | None = None
    counters: Sequence[Any] = ()  # element objects expose .name attribute
    metrics: Sequence[Any] = ()   # element objects expose .name attribute
    interval_us: int = 0
    auto_initialize: bool = False
    _configured: bool = field(default=False, init=False)

    def _configure(self):
        """Idempotently configure counters / metrics for immediate collection.

        We currently always use IMMEDIATE mode; background / periodic support can
        be added later by accepting additional parameters without breaking callers.
        """
        if self._configured:
            return
        params = CollectionParameters(sampling_interval=self.interval_us, mode=CollectionMode.IMMEDIATE)
        if self.counters:
            configure_counters_on_target(self.target, params, list(self.counters))
        if self.metrics:
            configure_metrics_on_target(self.target, params, list(self.metrics))
        self._configured = True

    def __enter__(self):
        if self.auto_initialize:
            try:
                initialize(None)
            except Exception:
                pass
        self._configure()
        try:
            start_collection(self.target)
        except Exception:
            pass
        return self

    def __exit__(self, exc_type, exc, tb):
        try:
            stop_collection(self.target)
        except Exception:
            pass
        return False

    def poll_once(self) -> Dict[str, Dict[str, List[Tuple[int, Any]]]]:
        """Perform a single immediate read for configured entities.

        Returns dict with keys 'counters' and 'metrics' mapping entity->samples list.
        """
        read_immediate(self.target)
        snapshot: Dict[str, Dict[str, List[Tuple[int, Any]]]] = {"counters": {}, "metrics": {}}
        if self.target is None:
            return snapshot
        tgt: Target = self.target
        for c in self.counters:
            try:
                snapshot["counters"][c.name] = get_counter_samples(tgt, c)  # type: ignore[arg-type]
            except Exception:
                snapshot["counters"][c.name] = []
        for m in self.metrics:
            try:
                snapshot["metrics"][m.name] = get_metric_samples(tgt, m)  # type: ignore[arg-type]
            except Exception:
                snapshot["metrics"][m.name] = []
        return snapshot

__all__ = ["Session"]
