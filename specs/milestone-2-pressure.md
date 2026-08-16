# Milestone 2 spec — pressure heuristic rework

Rework the pressure engine so a score answers one question: **"is memory slowing this app down?"** Read `PLAN.md` and `CLAUDE.md` first. Touch only `src/core/`, `src/cli/main.cpp` (band names if needed), `src/gui/main.cpp` (colors), and `PLAN.md` (rewrite the heuristic section to match this spec).

## Band semantics (drives everything)

Keep the existing `PressureBand` enum values; their meaning and GUI colors change:

| Band | Color | Meaning |
|---|---|---|
| Low | green | Memory is not slowing this app. |
| Moderate | yellow | Slowing it measurably, but below human-noticeable. Also the *latent* case: working set trimmed far below commit while system memory is tight — it will fault when the user returns to it. |
| High | orange | Noticeably affected **right now, but not sustained** — likely a spike (e.g. paging back in after an app switch); waiting may resolve. |
| Critical | red | Noticeably affected and **sustained** across several intervals. |

## Signals

- Switch every fault-based signal from `PageFaultCount` (includes soft faults — noise) to **`HardFaultCount`** from SYSTEM_PROCESS_INFORMATION (offset already present in the local `ProcessRecord` struct as `hard_fault_count`; it is currently unused).
  - `ProcessInfo`: replace `page_faults` with `hard_faults` (value of `hard_fault_count`).
  - `AppEntry`: replace `page_faults` with `hard_faults` (summed by the grouper).
  - `SystemStats`: replace `page_faults` with `hard_faults` (sum over processes). Delete `memory_compression_working_set` and the Memory Compression special-case in snapshot.cpp — the compression signal is removed entirely.
- Hard-fault **rates** are deltas between successive snapshots divided by elapsed seconds, computed in `MemPressEngine` exactly as today (QPC-based elapsed; needs 2 samples, zero on first).

## Per-app score and band

Constants (name them, one place): `kMeasurableFaultsPerSec = 10.0`, `kNoticeableFaultsPerSec = 100.0`, `kSustainedIntervals = 3`.

Per app, track in history: last hard-fault count, plus a consecutive counter of intervals where the app's rate >= `kNoticeableFaultsPerSec`.

Band decision:
1. rate >= noticeable and consecutive >= `kSustainedIntervals` → **Critical**.
2. rate >= noticeable otherwise → **High**.
3. rate >= measurable → **Moderate**.
4. rate < measurable but *latent trim* → **Moderate**. Latent trim = (working_set / commit) < 0.5 while system max(commit_ratio, physical_ratio) >= 0.8 (commit > 0).
5. otherwise → **Low**.

Numeric score must agree with the band: map into the band's range (Low 0–24, Moderate 25–49, High 50–74, Critical 75–100) by linear interpolation within the band:
- Low: rate 0→measurable maps 0→24.
- Moderate: rate measurable→noticeable maps 25→49 (latent-trim-only case: 25 + (1 − ws/commit ratio) × 24).
- High: rate noticeable→10× noticeable maps 50→74, clamped.
- Critical: rate noticeable→10× noticeable maps 75→100, clamped.

Contribution-by-commit-share and commit-growth terms are **removed** from the score.

## System score and band

Same band semantics, same constants scaled system-wide: `kSystemMeasurableFaultsPerSec = 50.0`, `kSystemNoticeableFaultsPerSec = 500.0`, sustained counter identical.

- `memory_load` = max(commit_total/commit_limit, 1 − physical_available/physical_total), each guarded against zero denominators.
- `load_score` = memory_load / 0.9 × 100, clamped 0–100 (unchanged anchor: 90% = saturated).
- `fault_score` = system hard-fault rate mapped 0→100 with `kSystemNoticeableFaultsPerSec` × 10 as the 100 point, clamped.
- Raw score = 0.5 × load_score + 0.5 × fault_score. Band from the raw score's range **except** fault-driven overrides: if system rate >= noticeable → at least High; and Critical from the score alone requires the sustained condition (score ≥ 75 with rate below noticeable-sustained caps at High). Clamp the final numeric score into the final band's range.
- The rolling 8-sample baseline (`page_fault_rates_` deque) is removed — anchors are absolute now.

## Front-ends

- GUI `PressureColor`: four distinct colors now — Low green, Moderate yellow, **High orange** (dark theme ≈ RGB(255,165,70), light ≈ RGB(200,100,0)), Critical red. Keep dark/light legibility variants.
- CLI: band names unchanged (`Low/Moderate/High/Critical`); nothing else changes. `--json` still emits `pressure` + `band`.

## Definition of done

- No references to `page_faults`, compression, commit-share contribution, or the rolling baseline remain in `src/`.
- Whole project builds clean at /W4 (`cmake -S . -B build`, `cmake --build build --config Release`; cmake at `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`).
- Run `winmempress-cli.exe` once on the idle machine: every ordinary app should now read Low (green range, < 25); flag in your summary if any app other than one actively working reads Moderate+.
- Run `winmempress-cli.exe --json` and confirm it parses.
- `PLAN.md` heuristic section rewritten to match this spec.
- Do not git commit; leave the working tree for review.
