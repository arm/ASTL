#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Generate/update ASTL metrics declaration JSON from an SCMI public spec JSON.

Examples:
  python3 scripts/scmi_json_to_metrics.py \
      --input config/scmi/public/mockscmi/mockscmi.json \
      --output config/metrics/mockscmi/metrics.json

  # Merge into existing output file while preserving existing metrics and metadata:
  python3 scripts/scmi_json_to_metrics.py \
      --input config/scmi/public/mockscmi/mockscmi.json \
      --output config/metrics/mockscmi/metrics.json \
      --merge
"""

from __future__ import annotations

import argparse
import json
from collections import OrderedDict, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any


# SCMI -> ASTL metric_type mapping requested by user
SCMI_TYPE_TO_METRIC_TYPE = {
    "Gauge": "value",
    "Counter": "delta",
}


@dataclass(frozen=True)
class MetricSignature:
    register: str
    unit: str | None
    base10_unit_modifier: int | None
    metric_type: str


@dataclass
class ConversionStats:
    generated: int = 0
    merged_existing: int = 0
    skipped_histogram: int = 0
    skipped_residency_placeholder: int = 0
    skipped_unhandled_type: int = 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Translate selected SCMI metric declarations into ASTL metrics declaration format."
    )
    parser.add_argument("--input", required=True, type=Path, help="Input SCMI spec JSON path (e.g. scp.json)")
    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="Output metrics declaration JSON path (created or updated)",
    )
    parser.add_argument(
        "--merge",
        action="store_true",
        help="Merge generated metrics into existing output file (preserve existing entries)",
    )
    parser.add_argument(
        "--document-confidential",
        choices=["true", "false"],
        default=None,
        help="Optional document.confidential override when creating a new output file.",
    )
    return parser.parse_args()


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as file:
        return json.load(file)


def get_metric_entries(scmi_json: dict[str, Any]) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    for member in scmi_json.get("members", []):
        component = member.get("component_name")
        for register, data in member.get("metrics", {}).items():
            metric_entry = dict(data)
            metric_entry.setdefault("register", register)
            metric_entry.setdefault("component", component)
            entries.append(metric_entry)
    return entries


def _is_residency_gauge_placeholder(metric: dict[str, Any]) -> bool:
    metric_type = metric.get("type")
    name = metric.get("name") or metric.get("register") or ""
    return metric_type == "Gauge" and str(name).startswith("RESIDENCY_")


def _is_histogram_placeholder(metric: dict[str, Any]) -> bool:
    return metric.get("type") == "Histogram"


def map_metric_type(metric: dict[str, Any], stats: ConversionStats) -> str | None:
    """Map SCMI type to output metric_type.

    Placeholder behavior:
    - Histogram: intentionally skipped for now (placeholder for future implementation)
    - RESIDENCY_* Gauge: intentionally skipped for now (placeholder for future implementation)
    """
    if _is_histogram_placeholder(metric):
        stats.skipped_histogram += 1
        return None

    if _is_residency_gauge_placeholder(metric):
        stats.skipped_residency_placeholder += 1
        return None

    scmi_type = metric.get("type")
    mapped = SCMI_TYPE_TO_METRIC_TYPE.get(scmi_type)
    if mapped is None:
        stats.skipped_unhandled_type += 1
    return mapped


def build_metric_signature(metric: dict[str, Any], mapped_metric_type: str) -> MetricSignature:
    return MetricSignature(
        register=str(metric.get("register") or metric.get("name") or ""),
        unit=metric.get("unit"),
        base10_unit_modifier=metric.get("base10_unit_modifier"),
        metric_type=mapped_metric_type,
    )


def build_output_metric(metric: dict[str, Any], mapped_metric_type: str) -> dict[str, Any]:
    register = str(metric.get("register") or metric.get("name") or "UNKNOWN_REGISTER")
    unit = metric.get("unit")

    output_metric: dict[str, Any] = {
        "description": metric.get("description", register),
        "metric_type": mapped_metric_type,
        "identifier": "unknown",
        "collection": {
            "register": register,
            "protocol": "scmi",
        },
    }

    if unit not in (None, ""):
        output_metric["unit"] = unit

    return output_metric


def add_generated_metrics(
    metric_entries: list[dict[str, Any]],
    existing_metrics: dict[str, Any],
    stats: ConversionStats,
) -> dict[str, Any]:
    grouped_by_name: dict[str, list[MetricSignature]] = defaultdict(list)
    metrics_out: "OrderedDict[str, dict[str, Any]]" = OrderedDict(existing_metrics)

    for metric in metric_entries:
        mapped_metric_type = map_metric_type(metric, stats)
        if mapped_metric_type is None:
            continue

        signature = build_metric_signature(metric, mapped_metric_type)
        register = signature.register
        if not register:
            stats.skipped_unhandled_type += 1
            continue

        # De-duplicate: same register + unit + base10 modifier + mapped type => single output definition
        if signature in grouped_by_name[register]:
            continue
        grouped_by_name[register].append(signature)

        # If same register appears with conflicting signature, disambiguate key name.
        # This keeps all information without clobbering prior entries.
        key_name = register
        if len(grouped_by_name[register]) > 1:
            component = metric.get("component") or "component"
            key_name = f"{register}__{component}"

        if key_name in metrics_out:
            stats.merged_existing += 1
            continue

        metrics_out[key_name] = build_output_metric(metric, mapped_metric_type)
        stats.generated += 1

    return metrics_out


def build_output_document(existing: dict[str, Any], confidential_override: str | None) -> dict[str, Any]:
    output = dict(existing)
    output.setdefault("_comment", "Generated from SCMI spec JSON")

    document = dict(output.get("document", {}))
    if confidential_override is not None:
        document["confidential"] = confidential_override.lower() == "true"
    elif "confidential" not in document:
        document["confidential"] = False

    output["document"] = document
    output.setdefault("metrics", {})
    return output


def main() -> int:
    args = parse_args()

    scmi_json = load_json(args.input)

    if args.merge and args.output.exists():
        output_root = load_json(args.output)
    else:
        output_root = {}

    output_root = build_output_document(output_root, args.document_confidential)

    stats = ConversionStats()
    metric_entries = get_metric_entries(scmi_json)
    output_root["metrics"] = add_generated_metrics(metric_entries, output_root.get("metrics", {}), stats)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as file:
        json.dump(output_root, file, indent=2)
        file.write("\n")

    print(f"Wrote: {args.output}")
    print(f"Generated metrics: {stats.generated}")
    print(f"Skipped (already existed): {stats.merged_existing}")
    print(f"Skipped Histogram placeholders: {stats.skipped_histogram}")
    print(f"Skipped RESIDENCY_* Gauge placeholders: {stats.skipped_residency_placeholder}")
    print(f"Skipped unhandled types/entries: {stats.skipped_unhandled_type}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
