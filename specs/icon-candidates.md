# Spec — 5 candidate app icons

Generate five distinct candidate icons for "MemPressMonitor" (this repo's GUI app: per-app memory usage + a green/yellow/orange/red memory-pressure heuristic).

## Deliverables

- `tools/generate-icons.ps1` — a self-contained PowerShell 7 script using System.Drawing (no third-party tools, no network) that draws each candidate and writes:
  - `assets/icon-candidates/candidate<N>.png` — 256×256 preview per candidate.
  - `assets/icon-candidates/candidate<N>.ico` — real multi-image .ico per candidate containing 16, 32, 48, and 256 px renders (write the ICO container manually: ICONDIR + ICONDIRENTRY table + embedded PNG images, which is valid for ICO on Vista+).
- Run the script (invoke as `pwsh.exe -NoProfile -File tools/generate-icons.ps1`) so the outputs exist, and sanity-check that each .ico file is non-empty and each PNG loads.

## Design directions (one icon each)

1. RAM stick silhouette with a small pressure-gauge needle overlay.
2. Rounded-square gauge: arc from green through yellow/orange to red with a needle.
3. Memory chip (square with pins) with a colored pressure dot in the corner.
4. Vertical bar meter (green→yellow→orange→red segments) next to a chip outline.
5. Minimal "M" monogram over a subtle green-to-red gradient ring.

## Style rules

- Flat, modern, Windows 11-friendly: simple geometry, rounded corners, no gradients except where the direction calls for one, transparent background.
- Must stay legible at 16×16: bold shapes, ≤4 colors per icon, no thin strokes below 2px at 256 scale.
- Draw programmatically with GraphicsPath/Pen/Brush primitives; anti-alias on; scale all geometry from a 256-unit design grid so the same draw function renders every size.

## Constraints

- Do not modify any existing source files, CMakeLists.txt, or resources. New files only, under `tools/` and `assets/icon-candidates/`.
- Do not git commit.
