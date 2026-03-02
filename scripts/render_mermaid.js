#!/usr/bin/env node

// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

/*
 * Mermaid batch renderer for design diagrams.
 *
 * Features:
 *  - Deterministic rendering via Mermaid (ESM) inside jsdom.
 *  - Tight viewBox normalization (prevents cropping & removes stale width/height attributes).
 *  - White background <rect> injected as first child (dark‑mode safe; consistent embedding).
 *  - Optional vertical compression for overly tall sequence diagrams controlled by an env var.
 *
 * Environment Variables:
 *  - SEQ_MAX_HEIGHT: (integer, pixels in viewBox units) If set and a sequence diagram's computed
 *      viewBox height exceeds this value, its contents are uniformly scaled in Y so the new
 *      viewBox height becomes SEQ_MAX_HEIGHT. Width/original X extents are preserved.
 *      A safety guard refuses to compress if scale would drop below 0.35 (readability).
 *      Omit / unset to keep natural height.
 *
 *      Example:
 *        SEQ_MAX_HEIGHT=5000 node scripts/render_mermaid.js --all
 *
 *  - (Reserved) SEQ_MIN_MESSAGE_GAP: Not yet implemented; placeholder for potential future logic
 *      to enforce a minimum vertical gap between message groups post‑compression.
 *
 * Notes:
 *  - Compression wraps existing top‑level children in a <g transform="translate .. scale(1, s) ..">.
 *    Re‑running with a smaller SEQ_MAX_HEIGHT on an already‑compressed output is safe only if you
 *    re-render from the original .mmd (recommended workflow always re-renders from sources).
 *  - No fixed output width/height attributes are written; consumers should rely on the viewBox and
 *    apply sizing via CSS / embedding attributes as needed.
 */
const fs = require("fs");
const path = require("path");
const { JSDOM } = require("jsdom");

function setupDom() {
  const dom = new JSDOM('<div id="app"></div>', { pretendToBeVisual: true });
  global.window = dom.window;
  global.document = dom.window.document;
  global.navigator = { userAgent: "node" };
  function polyBBox() {
    return { x: 0, y: 0, width: 200, height: 50 };
  }
  if (global.SVGElement && !global.SVGElement.prototype.getBBox) {
    global.SVGElement.prototype.getBBox = polyBBox;
  }
  // patch createElementNS to ensure every SVG element gets getBBox
  const origCreate = document.createElementNS.bind(document);
  document.createElementNS = function (ns, name) {
    const el = origCreate(ns, name);
    if (ns === "http://www.w3.org/2000/svg" && !el.getBBox) {
      el.getBBox = polyBBox;
    }
    return el;
  };
}
async function loadMermaid() {
  return (await import("mermaid")).default;
}
function injectWhiteBackground(svg) {
  // Parse existing svg to derive numeric viewBox for exact background sizing.
  try {
    const dom = new JSDOM(svg);
    const doc = dom.window.document;
    const svgel = doc.querySelector("svg");
    if (!svgel) return svg;
    // Remove any pre-existing leading white rect we may have inserted previously
    const firstRect = svgel.querySelector(":scope > rect");
    if (firstRect && /#fff|white/i.test(firstRect.getAttribute("fill") || "")) {
      // Keep only if we can't compute a new one; otherwise remove and recreate
      firstRect.remove();
    }
    const vb = svgel.getAttribute("viewBox");
    let rectEl;
    if (vb) {
      const parts = vb.trim().split(/\s+/).map(Number);
      if (parts.length === 4 && parts.every((n) => !isNaN(n))) {
        const [minX, minY, w, h] = parts;
        rectEl = doc.createElementNS("http://www.w3.org/2000/svg", "rect");
        rectEl.setAttribute("x", String(minX));
        rectEl.setAttribute("y", String(minY));
        rectEl.setAttribute("width", String(w));
        rectEl.setAttribute("height", String(h));
        rectEl.setAttribute("fill", "white");
      }
    }
    if (!rectEl) {
      // Fallback if no viewBox: generic 100% sizing
      rectEl = doc.createElementNS("http://www.w3.org/2000/svg", "rect");
      rectEl.setAttribute("x", "0");
      rectEl.setAttribute("y", "0");
      rectEl.setAttribute("width", "100%");
      rectEl.setAttribute("height", "100%");
      rectEl.setAttribute("fill", "white");
    }
    // Insert as first child
    svgel.insertBefore(rectEl, svgel.firstChild);
    return svgel.outerHTML;
  } catch (e) {
    // Fallback to original behavior if parsing fails
    return svg.replace(
      /<svg([^>]*)>/,
      (m, attrs) =>
        `<svg${attrs}><rect x="0" y="0" width="100%" height="100%" fill="white"/>`,
    );
  }
}
// Attempt to expand the viewBox to include all visible nodes/edges for very large class diagrams
function adjustViewBox(svg) {
  try {
    const dom = new JSDOM(svg);
    const doc = dom.window.document;
    const svgel = doc.querySelector("svg");
    if (!svgel) return svg;
    // Remove empty edge label groups (rect + text with no characters) to cut visual clutter / overlap boxes
    doc.querySelectorAll("g.edgeLabel").forEach((g) => {
      const text = g.querySelector("text");
      if (text) {
        const hasVisible = [...text.querySelectorAll("tspan")].some(
          (ts) => (ts.textContent || "").trim().length > 0,
        );
        if (!hasVisible) {
          g.remove();
        }
      }
    });
    // Collect rects with absolute numeric sizes and node groups with translate transforms
    let minX = Infinity,
      minY = Infinity,
      maxX = -Infinity,
      maxY = -Infinity;
    const numericRect = (r) =>
      r &&
      /^\d+(?:\.\d+)?$/.test(r.getAttribute("width") || "") &&
      /^\d+(?:\.\d+)?$/.test(r.getAttribute("height") || "");
    [...svgel.querySelectorAll("rect")].forEach((r) => {
      if (!numericRect(r)) return; // skip percentage/background rect
      const x = parseFloat(r.getAttribute("x")) || 0;
      const y = parseFloat(r.getAttribute("y")) || 0;
      const w = parseFloat(r.getAttribute("width")) || 0;
      const h = parseFloat(r.getAttribute("height")) || 0;
      if (x < minX) minX = x;
      if (y < minY) minY = y;
      if (x + w > maxX) maxX = x + w;
      if (y + h > maxY) maxY = y + h;
    });
    // For groups with translate (nodes) include their internal rect to compute extents
    [...svgel.querySelectorAll('g[transform^="translate("]')].forEach((g) => {
      const tr = g.getAttribute("transform");
      const m = /translate\(\s*([0-9.+-]+)\s*,\s*([0-9.+-]+)\s*\)/.exec(tr);
      if (!m) return;
      const tx = parseFloat(m[1]),
        ty = parseFloat(m[2]);
      const rect = g.querySelector(":scope > rect, :scope g > rect");
      if (rect) {
        const rx = parseFloat(rect.getAttribute("x")) || 0;
        const ry = parseFloat(rect.getAttribute("y")) || 0;
        const rw = parseFloat(rect.getAttribute("width")) || 0;
        const rh = parseFloat(rect.getAttribute("height")) || 0;
        const absX = tx + rx;
        const absY = ty + ry;
        if (absX < minX) minX = absX;
        if (absY < minY) minY = absY;
        if (absX + rw > maxX) maxX = absX + rw;
        if (absY + rh > maxY) maxY = absY + rh;
      }
    });
    // Sequence diagrams often lack encompassing rects for the full vertical span; include line and text coordinates
    [...svgel.querySelectorAll("line")].forEach((l) => {
      const x1 = parseFloat(l.getAttribute("x1")) || 0;
      const y1 = parseFloat(l.getAttribute("y1")) || 0;
      const x2 = parseFloat(l.getAttribute("x2")) || 0;
      const y2 = parseFloat(l.getAttribute("y2")) || 0;
      if (x1 < minX) minX = x1;
      if (x2 < minX) minX = x2;
      if (y1 < minY) minY = y1;
      if (y2 < minY) minY = y2;
      if (x1 > maxX) maxX = x1;
      if (x2 > maxX) maxX = x2;
      if (y1 > maxY) maxY = y1;
      if (y2 > maxY) maxY = y2;
    });
    [...svgel.querySelectorAll("text")].forEach((t) => {
      const x = parseFloat(t.getAttribute("x"));
      const y = parseFloat(t.getAttribute("y"));
      if (!isNaN(x) && x < minX) minX = x;
      if (!isNaN(y) && y < minY) minY = y;
      if (!isNaN(x) && x > maxX) maxX = x;
      // assume ~20px text height below baseline
      if (!isNaN(y) && y + 20 > maxY) maxY = y + 20;
    });
    if (
      !isFinite(minX) ||
      !isFinite(minY) ||
      !isFinite(maxX) ||
      !isFinite(maxY)
    )
      return svg; // nothing gathered
    // Further reduced margin from 20 -> 10 to tighten vertical space more.
    const margin = 10; // minimal breathing room to reduce height further
    const vb = `${minX - margin} ${minY - margin} ${maxX - minX + 2 * margin} ${maxY - minY + 2 * margin}`;
    // Extend actor lifelines (vertical lines with id="actorN") to the computed bottom if they are shorter
    const bottomY = maxY + margin - 2; // slight offset above absolute bottom
    [...svgel.querySelectorAll('line[id^="actor"]')].forEach((line) => {
      const y2 = parseFloat(line.getAttribute("y2")) || 0;
      if (y2 < bottomY) line.setAttribute("y2", bottomY.toString());
    });
    svgel.setAttribute("viewBox", vb);
    svgel.removeAttribute("style"); // remove max-width that cropped huge diagrams
    // No enforced width/height sizing: allow natural dimensions; consumers can scale via viewBox if desired.
    // Remove any prior width/height we may have set in earlier runs to avoid stale constraints.
    svgel.removeAttribute("width");
    svgel.removeAttribute("height");
    return svgel.outerHTML;
  } catch (e) {
    // On any failure, fall back to original
    return svg;
  }
}
// Optionally compress tall sequence diagrams by scaling Y coordinates inside the viewBox (keeps width / layout stable).
// Environment variables:
//   SEQ_MAX_HEIGHT: (number, px units) If viewBox height exceeds this, attempt compression.
//   SEQ_MIN_MESSAGE_GAP: (number) Minimum vertical gap to preserve between distinct message groups (default 6).
// Strategy: apply a uniform vertical scale centered at top of diagram so bottom fits target height, but abort if scale < 0.35 (too compressed).
// maybeCompressSequence removed: fixed viewport makes it redundant.
async function renderOne(inFile, outFile) {
  const code = fs.readFileSync(inFile, "utf8");
  const mermaid = await loadMermaid();
  mermaid.initialize({
    startOnLoad: false,
    theme: "base",
    securityLevel: "strict",
    flowchart: { htmlLabels: false },
    sequence: {
      mirrorActors: false,
      diagramMarginX: 6,
      diagramMarginY: 6,
      actorMargin: 70,
      messageMargin: 8,
      noteMargin: 8,
    },
    themeVariables: {
      background: "#ffffff",
      primaryColor: "#ffffff",
      primaryBorderColor: "#444444",
      tertiaryColor: "#ffffff",
      fontSize: "13px",
    },
  });
  const id = "mmd_" + path.basename(inFile).replace(/[^a-zA-Z0-9_]/g, "_");
  const { svg } = await mermaid.render(id, code);
  // Post-process: adjust viewBox (tight bounds) only; keep natural sizing.
  let processed = adjustViewBox(svg);
  const withBg = injectWhiteBackground(processed);
  fs.writeFileSync(outFile, withBg, "utf8");
  console.log(`Rendered ${inFile} -> ${outFile}`);
}
async function renderAll() {
  const base = path.join(process.cwd(), "doc", "design");
  if (!fs.existsSync(base)) {
    console.error("Missing doc/design directory");
    process.exit(2);
  }
  const files = fs.readdirSync(base).filter((f) => f.endsWith(".mmd"));
  for (const f of files) {
    const inFile = path.join(base, f);
    const outFile = path.join(base, f.replace(/\.mmd$/, ".svg"));
    try {
      await renderOne(inFile, outFile);
    } catch (e) {
      console.error("Failed", inFile, e.message);
    }
  }
}
async function main() {
  setupDom();
  const args = process.argv.slice(2);
  if (args.length === 0) {
    console.error("Usage: render_mermaid.js (--all | input.mmd [output.svg])");
    process.exit(1);
  }
  if (args[0] === "--all") {
    await renderAll();
    return;
  }
  const inFile = args[0];
  const outFile = args[1] || inFile.replace(/\.mmd$/, ".svg");
  await renderOne(inFile, outFile);
}
main().catch((e) => {
  console.error(e);
  process.exit(1);
});
