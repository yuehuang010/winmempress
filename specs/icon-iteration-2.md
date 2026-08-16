# Spec — icon iteration round 2

The user picked candidate 2 (rounded-square pressure gauge) and candidate 4 (bar meter + chip) as favorites. Produce six new variants exploring those two directions — three each — as `candidate6.png/.ico` through `candidate11.png/.ico`.

## How

Extend `tools/generate-icons.ps1` (keep candidates 1–5 working) with the new variants, then run it (`pwsh.exe -NoProfile -File tools/generate-icons.ps1`) and sanity-check outputs the same way as before: 256px PNG preview + multi-image ICO (16/32/48/256, PNG-embedded) per candidate, into `assets/icon-candidates/`.

## Gauge variants (from candidate 2)

6. Same gauge but on a filled dark navy rounded square (white/colored arc pops on dark), no outer frame ring.
7. Gauge arc thicker and needle bolder, frame ring in neutral dark grey instead of green, arc covering ~270° instead of ~180°.
8. Half-gauge (bottom-flat speedometer look) on a filled green rounded square, white needle.

## Bar meter variants (from candidate 4)

9. Bars moved INSIDE a dark chip silhouette (chip with pins, 4 horizontal green→red bars filling its body).
10. Same layout as candidate 4 but the chip is filled dark navy (not outline) and the bars are taller/chunkier.
11. Vertical bar stack alone on a dark navy rounded square — no chip, bars as the sole element, slight corner radius per bar.

## Style rules

Same as round 1: flat, ≤4 colors + background, legible at 16×16, no strokes under 2px at 256 scale, transparent canvas outside the main shape, all geometry from the shared 256-unit grid.

## Constraints

- New files only (script edit + new assets). Do not touch other sources. Do not git commit.
