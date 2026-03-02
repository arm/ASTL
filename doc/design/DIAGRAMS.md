<!--
SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# Design Diagrams

This directory contains Mermaid source diagrams (`.mmd`) and their rendered SVG outputs.
The rendering is deterministic and handled by a custom Node.js script that ensures:

- White background rectangle (avoids dark-mode inversion / transparent issues)
- Correct `viewBox` expansion (prevents cropping on tall/wide sequence diagrams)
- Stable theming (base theme, consistent fonts)
- Natural per-diagram sizing (no forced uniform thumbnail dimensions)

## Source Diagrams

| Category          | Purpose                              | Source (.mmd)                         | Output (.svg)                         | Notes                                                                              |
| ----------------- | ------------------------------------ | ------------------------------------- | ------------------------------------- | ---------------------------------------------------------------------------------- |
| Core              | Orchestrator class / relationships   | `orchestrator.mmd`                    | `orchestrator.svg`                    | Class-level view (Mermaid `classDiagram`).                                         |
| Core              | Metric Manager lifecycle             | `MetricManager_sequence.mmd`          | `MetricManager_sequence.svg`          | Sequence: registration → configuration → collection → summarization → destruction. |
| System (overview) | End-to-end system phases overview    | `system_end_to_end_sequence.mmd`      | `system_end_to_end_sequence.svg`      | High-level API ↔ internal components, references phase diagrams below.            |
| System Phase 1    | Initialization & discovery           | `system_phase_init_discovery.mmd`     | `system_phase_init_discovery.svg`     | Init managers, build topology, enumerate targets & metrics.                        |
| System Phase 2    | Metric configuration                 | `system_phase_metric_config.mmd`      | `system_phase_metric_config.svg`      | Derive operations, configure collectors, error branch.                             |
| System Phase 3    | Interval collection & immediate read | `system_phase_collection.mmd`         | `system_phase_collection.svg`         | Start, sampling loop, immediate read, pause/resume placeholders.                   |
| System Phase 4    | Stop & deferred processing           | `system_phase_stop_processing.mmd`    | `system_phase_stop_processing.svg`    | Process raw → processed, summaries, deferred model emphasis.                       |
| System Phase 5    | Retrieval & shutdown                 | `system_phase_retrieval_shutdown.mmd` | `system_phase_retrieval_shutdown.svg` | Retrieval APIs, teardown, representative errors.                                   |

## Rationale for Phased Split

The original monolithic system sequence diagram became extremely tall, reducing readability when scaled. Splitting into focused phase diagrams keeps each concern legible without forced global downscaling.

## Regenerating All Diagrams

From the repository root (requires Node ≥18 and the `mermaid` package):

```sh
node scripts/render_mermaid.js --all
```

## Regenerating a Single Diagram

```sh
node scripts/render_mermaid.js doc/design/MetricManager_sequence.mmd
# or specify explicit output
node scripts/render_mermaid.js doc/design/MetricManager_sequence.mmd doc/design/MetricManager_sequence.svg
```

## Renderer Script

`scripts/render_mermaid.js` performs these steps:

1. Creates a headless DOM using `jsdom`.
2. Loads Mermaid ESM dynamically and initializes with a base theme.
3. Renders each `.mmd` into SVG.
4. Injects a white background `<rect>` as the first child of `<svg>`.
5. Computes aggregate bounds across `<rect>`, translated `<g>`, `<line>`, and `<text>` elements to build a safe `viewBox` (adds margin) to avoid clipping. Removes explicit `width`/`height` attributes so the SVG scales naturally.
6. Writes the SVG alongside the source file.

## CI / Drift Detection Suggestion

Add a CI job to ensure committed SVGs are current:

```sh
node scripts/render_mermaid.js --all
if git diff --quiet -- doc/design/*.svg; then
  echo "Diagrams up to date";
else
  echo "Out-of-date diagram(s). Run: node scripts/render_mermaid.js --all" >&2
  exit 1
fi
```

(Place this in a workflow step after installing dependencies.)

## Optional Future Enhancements

- Parallel thumbnail export (e.g. `THUMBNAILS=1` to emit `*-thumb.svg`).
- PNG / raster export via `resvg` or `rsvg-convert`.
- Lint step for Mermaid syntax before render.
- Diagram diffing in PR comments (render both base & head, attach images).

## Known Limitations

- Script assumes all `.mmd` files live in `doc/design/` (adjust `renderAll()` if you add nested folders).
- Sequence diagram vertical bounds heuristic adds ~40px margin; extreme negative translations may still need manual tweaks.
- No built-in pagination for very large flows (mitigated by phased split).
- Mermaid version upgrades may slightly shift layout; regenerate all diagrams in a single commit when upgrading.

## Adding New Diagrams

1. Create a new `*.mmd` file in this directory.
2. Run `node scripts/render_mermaid.js --all`.
3. Commit both the `.mmd` and generated `.svg`.

## Style Consistency

To tweak colors or fonts, edit `themeVariables` inside `scripts/render_mermaid.js` and regenerate all diagrams in a single commit.

---

Maintainers: Update this document if you add export formats, relocate diagrams, or change the rendering pipeline.
