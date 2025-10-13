"""Derived metric helpers.

Utilities to compute simple derived values (delta, rate) from raw sample lists.

Input format
------------
``samples`` is a list of ``(timestamp, value)`` tuples where:
    * ``timestamp`` is an integer counter from ASTL (assumed monotonic non-decreasing)
    * ``value`` is a numeric type (int/float)

Returned lists align with the *intervals* between successive samples (length is
``len(samples) - 1`` when at least two points exist). Empty or single-point
inputs yield an empty result.

Example::

        samples = [(100, 10), (200, 25), (350, 40)]
        deltas(samples) -> [(200, 15.0), (350, 15.0)]
        rates(samples, time_scale=1.0) -> [(200, 0.15), (350, 0.1153846...)]

Edge cases:
    * Non-numeric values produce ``nan`` deltas/rates entries.
    * Zero delta-time produces ``inf`` for rate to signal division by zero.
    * Exceptions during conversion are contained per-interval.
"""
from __future__ import annotations

from typing import List, Tuple


def deltas(samples: List[Tuple[int, float | int]]) -> List[Tuple[int, float]]:
    """Compute successive value deltas.

    Returns a list whose i-th element corresponds to ``samples[i+1]`` using
    ``delta_value = value[i+1] - value[i]``. Non-numeric conversions yield ``nan``.
    """
    deltas_out: List[Tuple[int, float]] = []
    if len(samples) < 2:
        return deltas_out
    previous_timestamp, previous_value = samples[0]
    for timestamp, value in samples[1:]:
        try:
            delta_val = float(value) - float(previous_value)
        except Exception:
            delta_val = float('nan')
        deltas_out.append((timestamp, delta_val))
        previous_timestamp, previous_value = timestamp, value
    return deltas_out


def rates(samples: List[Tuple[int, float | int]], time_scale: float = 1.0) -> List[Tuple[int, float]]:
    """Compute per-interval rate values.

    ``rate[i] = (value[i+1]-value[i]) / ((timestamp[i+1]-timestamp[i]) / time_scale)``

    When ``delta_time`` is zero ``inf`` is used. Conversion failures produce ``nan``.
    """
    rates_out: List[Tuple[int, float]] = []
    if len(samples) < 2:
        return rates_out
    previous_timestamp, previous_value = samples[0]
    for timestamp, value in samples[1:]:
        delta_time = (timestamp - previous_timestamp)
        try:
            delta_val = float(value) - float(previous_value)
            rate_val = delta_val / (delta_time / time_scale) if delta_time != 0 else float('inf')
        except Exception:
            rate_val = float('nan')
        rates_out.append((timestamp, rate_val))
        previous_timestamp, previous_value = timestamp, value
    return rates_out

__all__ = ["deltas", "rates"]
