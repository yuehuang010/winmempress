# Spec: GPU shared-memory tracking

Status: **decided — use PDH.** The dedicated/shared split comes from documented
counters (same source as Task Manager), so no segment classification is needed.

## Goal

Surface GPU memory usage **only where it consumes system RAM**. This app tracks system
RAM; dedicated VRAM (discrete cards, iGPU carve-out) is out of scope.

- Track: per-process **shared segment** usage — WDDM allocations backed by system RAM,
  including iGPU spill beyond its dedicated carve-out.
- Never track or display: dedicated VRAM usage.

## Background

WDDM classifies every video memory allocation into a *dedicated* or *shared* segment.
The shared segment is ordinary system RAM, commit-charged to the owning process. Task
Manager's "Shared GPU memory" column shows the same classification, so the OS has
already done the dedicated/shared split — no heuristic needed.

## Data source decision: PDH (comparison kept for the record)

| | PDH (`\GPU Process Memory(pid_*)\Shared Usage`) | `D3DKMTQueryStatistics` (gdi32) |
|---|---|---|
| Documentation | Fully documented counter set (same source as Task Manager) | Semi-documented; struct layout has grown across Windows versions |
| Header / dep | `pdh.h` + `pdh.lib` (OS library — allowed) | `d3dkmthk.h` (ships in SDK 10.0.26100 `shared\`) + `gdi32.lib` |
| Dedicated/shared split | Separate counters; simply never query `Dedicated Usage` | Must pick through per-segment statistics to make the split |
| Query model | String counter paths, wildcard instance expansion, collect-per-tick | Direct struct call per adapter + process |
| Cost per 2s tick | One `PdhCollectQueryData` for a wildcard query; moderate | One call per adapter; cheap |
| Risk | Counter instance churn as processes start/exit (re-expand wildcard) | Struct-version drift across Windows releases |
| Localization | Use `PdhAddEnglishCounter*` (never localized `PdhAddCounter`) | N/A |

Decision: **PDH**, for the documented counters and the free dedicated/shared split.

## PDH implementation requirements

- Counter path: `\GPU Process Memory(pid_*)\Shared Usage` added with
  `PdhAddEnglishCounterW` (never `PdhAddCounterW` — localized paths break on
  non-English Windows). Never add `Dedicated Usage`.
- One `PDH_HQUERY` owned by the collector, opened lazily on first snapshot;
  `PdhCollectQueryData` once per tick, then
  `PdhGetFormattedCounterArrayW(..., PDH_FMT_LARGE, ...)` to expand instances.
- Instance names look like `pid_1234_luid_0x00000000_0x0000ABCD_phys_0`; parse the
  PID after the `pid_` prefix and **sum across instances with the same PID**
  (multi-adapter processes appear once per LUID).
- The wildcard handles instance churn — no per-process counter management. If any
  PDH call fails (counter set absent, remote session), close the query, leave all
  `gpu_shared` at 0, and do not retry more than once per snapshot.
- Link `pdh.lib` in `memcore` only; include `<pdh.h>` in `snapshot.cpp` only.

## Design

1. `ProcessInfo` gains `gpu_shared = 0` (bytes of shared-segment GPU usage).
2. Collection runs in the existing snapshot pass in `src/core/snapshot.cpp`; no
   per-process handles may be opened for this (PDH and D3DKMT both avoid them).
   If the source fails (no GPU, counter set missing, remote session), all
   `gpu_shared` stay 0 and nothing else changes — no error surfaced.
3. Grouper sums `gpu_shared` per app row like commit (plain sum — GPU shared is
   commit-charged per process, so summing does not double-count across processes;
   the DLL amortization rule does NOT apply).
4. Display: new "GPU shared" column in CLI table and JSON (`gpu_shared`, raw bytes in
   JSON, MB via `mempress::FormatMegabytes` in the table). GUI column optional,
   decided separately.

## Double-count rule (PLAN.md discipline)

Shared-segment GPU bytes are **already inside** each process's commit charge, and the
resident portion is inside its working set. The new number is therefore an
*informational overlay*, never added into the working-set or commit columns. Document
this next to the kernel-pool rule in PLAN.md when implementing.

## Acceptance

- `/W4 /permissive-` clean; no third-party deps; Unicode build.
- On a machine with any active GPU workload, at least one app row reports nonzero
  GPU shared, and the value roughly matches Task Manager's "Shared GPU memory" for
  the same processes.
- On a GPU-less/remote session: all zeros, no errors, no perf regression in the
  2-second tick.
- No dedicated-VRAM number appears anywhere in output.
