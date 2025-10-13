"""High-level polling / streaming helpers for ASTL.

Rationale:
        The core ASTL Python bindings expose primitive operations: configure, start,
        read_immediate, retrieve samples. For iterative data collection workflows
        users often want either:
            * a simple blocking loop that periodically fetches samples, or
            * an asynchronous stream that can be consumed with ``async for`` while
                other tasks run concurrently.

        This module layers small focused helpers that compose those primitives
        without obscuring the underlying data model (timestamps + raw values).

Provided capabilities:
        * Single-shot poll helpers (``poll_counter_once`` / ``poll_metric_once``)
        * Periodic synchronous generators (``poll_counter_periodic`` / ``poll_metric_periodic``)
        * Asynchronous iterators (``stream_counter`` / ``stream_metric``)
        * A minimal configuration helper (``configure_basic_collection``)

Design / behavioral notes:
        * Lifecycle calls (start/stop) rely on the underlying mapping of status
            codes to exceptions; typical NOT_IMPLEMENTED statuses surface as
            ``NotImplementedErrorASTL`` if not pre-suppressed by callers.
        * Each loop iteration issues a ``read_immediate`` just prior to sample
            retrieval. If the native library later supports autonomous background
            sampling we can adapt timing or optionally skip the call for efficiency.
        * Interval compensation is basic: we measure elapsed iteration time and
            sleep the remainder to *approximate* a fixed cadence. This is sufficient
            for lightweight telemetry demos; users needing tighter guarantees can
            implement a custom scheduler.
        * These helpers are stateless—no internal buffering, diffing, or duplicate
            suppression. Returned sample lists are verbatim from the core API.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Iterator, AsyncIterator, List, Tuple, Optional, Sequence, Callable
import time
import asyncio

from . import (
    start_collection,
    stop_collection,
    read_immediate,
    get_counter_samples,
    get_metric_samples,
    CollectionParameters,
    CollectionMode,
    configure_counters_on_target,
    configure_metrics_on_target,
)


@dataclass
class PollResult:
    timestamp: float  # Host wall-clock (seconds since epoch) when poll finished.
    samples: List[Tuple[int, object]]  # Raw (monotonic_timestamp, decoded_value) tuples from ASTL.


def poll_counter_once(target, counter) -> PollResult:
    """Fetch latest samples for a single counter.

    Sequence:
        1. Issue ``read_immediate`` (best effort, safe if NOT_IMPLEMENTED)
        2. Retrieve all currently buffered samples for the counter
        3. Wrap in ``PollResult`` with host timestamp
    """
    read_immediate(target)
    samples = get_counter_samples(target, counter)
    return PollResult(time.time(), samples)


def poll_metric_once(target, metric) -> PollResult:
    """Metric analogue of :func:`poll_counter_once`."""
    read_immediate(target)
    samples = get_metric_samples(target, metric)
    return PollResult(time.time(), samples)


def _sync_poll_loop(
    target,
    interval_sec: float,
    iterations: int | None,
    fetch: Callable[[], PollResult],
) -> Iterator[PollResult]:
    """Shared synchronous periodic polling loop.

    Parameters
    ----------
    target : Target
        Target object whose collection lifecycle we manage.
    interval_sec : float
        Desired wall-clock interval between successive poll *starts*.
    iterations : int | None
        Maximum number of iterations (``None`` for infinite).
    fetch : () -> PollResult
        Zero-arg callable returning the next PollResult (e.g. wraps poll_counter_once).
    """
    count = 0
    try:
        start_collection(target)
        while iterations is None or count < iterations:
            start = time.time()
            yield fetch()
            count += 1
            elapsed = time.time() - start
            remaining = interval_sec - elapsed
            if remaining > 0:
                time.sleep(remaining)
    finally:  # always attempt to stop collection even if fetch raises
        stop_collection(target)


def poll_counter_periodic(target, counter, interval_sec: float, iterations: int | None = None) -> Iterator[PollResult]:
    """Yield counter samples periodically (thin wrapper over shared loop)."""
    yield from _sync_poll_loop(
        target=target,
        interval_sec=interval_sec,
        iterations=iterations,
        fetch=lambda: poll_counter_once(target, counter),
    )


def poll_metric_periodic(target, metric, interval_sec: float, iterations: int | None = None) -> Iterator[PollResult]:
    """Metric variant of :func:`poll_counter_periodic` using shared loop."""
    yield from _sync_poll_loop(
        target=target,
        interval_sec=interval_sec,
        iterations=iterations,
        fetch=lambda: poll_metric_once(target, metric),
    )


async def _async_stream_loop(
    target,
    interval_sec: float,
    iterations: int | None,
    fetch: Callable[[], PollResult],
) -> AsyncIterator[PollResult]:
    """Shared asynchronous streaming loop.

    Mirrors :func:`_sync_poll_loop` but uses ``asyncio.sleep`` for cooperative
    scheduling in async contexts.
    """
    count = 0
    try:
        start_collection(target)
        while iterations is None or count < iterations:
            loop_start = time.time()
            yield fetch()
            count += 1
            remaining = interval_sec - (time.time() - loop_start)
            if remaining > 0:
                await asyncio.sleep(remaining)
    finally:
        stop_collection(target)


async def stream_counter(target, counter, interval_sec: float, iterations: int | None = None) -> AsyncIterator[PollResult]:
    """Async stream of counter samples (wrapper over shared async loop)."""
    async for result in _async_stream_loop(
        target=target,
        interval_sec=interval_sec,
        iterations=iterations,
        fetch=lambda: poll_counter_once(target, counter),
    ):
        yield result


async def stream_metric(target, metric, interval_sec: float, iterations: int | None = None) -> AsyncIterator[PollResult]:
    """Metric analogue of :func:`stream_counter` using shared async loop."""
    async for result in _async_stream_loop(
        target=target,
        interval_sec=interval_sec,
        iterations=iterations,
        fetch=lambda: poll_metric_once(target, metric),
    ):
        yield result


def configure_basic_collection(target, counters=(), metrics=(), sampling_interval_us: int = 0):
    """Configure minimal collection for provided counters / metrics.

    Currently sets mode to IMMEDIATE. When background sampling becomes available
    this helper can accept a mode parameter or auto-select based on interval.
    Returns the ``CollectionParameters`` instance used.
    """
    params = CollectionParameters(sampling_interval=sampling_interval_us, mode=CollectionMode.IMMEDIATE)
    if counters:
        configure_counters_on_target(target, params, list(counters))
    if metrics:
        configure_metrics_on_target(target, params, list(metrics))
    return params

__all__ = [
    "PollResult",
    "poll_counter_once",
    "poll_metric_once",
    "poll_counter_periodic",
    "poll_metric_periodic",
    "stream_counter",
    "stream_metric",
    "configure_basic_collection",
]
