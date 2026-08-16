# WinMemPress — Plan

A small Windows app that shows **apps** (not raw processes), their total memory usage, and a **memory pressure** score. Windows has no native "memory pressure" metric, so we define a heuristic.

## Tech stack

- **C++20, pure Win32** — no .NET, no external frameworks. UI is a plain Win32 window with a ListView common control (comctl32 v6 via manifest); everything links against OS-provided libraries only (`kernel32`, `user32`, `comctl32`, `psapi`, `ntdll`, `shell32`, `version`).
- **CMake + MSVC** build producing three targets from one tree:
  - `memcore` — static library: collector, grouper, pressure engine. No UI or console dependencies.
  - `winmempress-cli.exe` — console front-end over `memcore` (table output, optional watch mode).
  - `winmempress.exe` — Win32 GUI front-end over `memcore`.
- Runs unelevated by default; system/other-user processes it can't open are aggregated into a single "System & other" row.

## Source layout

```
CMakeLists.txt
src/
  core/            # memcore static lib — all logic lives here
    snapshot.h/.cpp    # NtQuerySystemInformation + per-process detail collection
    grouper.h/.cpp     # process → app grouping
    pressure.h/.cpp    # system + per-app pressure scoring
    appmodel.h         # AppEntry / SystemStats structs shared by both front-ends
  cli/
    main.cpp           # argument parsing, table printer, --watch loop
  gui/
    main.cpp           # WinMain, message loop, window class
    listview.cpp       # ListView columns, sorting, icon list, diff refresh
    app.rc, app.manifest  # comctl32 v6, per-monitor DPI awareness
```

Rule: `core/` includes no `<commctrl.h>` and no `printf` — it fills structs; front-ends render them. This keeps the CLI the test harness for milestones 1–2 and the GUI a thin view.

## Architecture

Three layers, each testable on its own:

```
Collector (per-process snapshot)  →  Grouper (process → app)  →  Pressure engine  →  CLI table / GUI ListView (2s refresh)
```

### 1. Collector

Snapshot every ~2s via `NtQuerySystemInformation(SystemProcessInformation)` in one call (cheap, no per-process handle needed for the basics), plus `GetProcessMemoryInfo` for processes we can open:

- Private working set (what Task Manager's "Memory" column shows) — the headline number.
- Private commit (PagefileUsage) — the real footprint claim.
- Page fault deltas per interval (hard-fault proxy).
- Parent PID, session ID, exe path.

### 2. Grouper — "apps, not processes"

Mimic Task Manager's Apps view:

1. **Packaged apps (UWP/MSIX):** group by package family name (`GetPackageFamilyName` on the process handle). One row per package.
2. **Desktop apps:** a process is an *app root* if it owns a visible, unowned top-level window (`EnumWindows` + `GetWindowThreadProcessId` + `IsWindowVisible` + no owner). All descendants via the parent-PID chain fold into the root (so Chrome/Edge/Teams child processes merge into one row). Guard against PID reuse by checking process start times when walking parents.
3. **Same-exe merge:** roots with identical exe paths merge (multiple Notepad windows = one "Notepad" row).
4. **Everything else** (services, background processes) collapses into one "Background & system" row so totals still add up.

App identity for display: exe `FileDescription` (fallback: exe name), icon extracted from the exe / package logo.

### 3. Memory pressure heuristic

Two scores, both 0–100 mapped to Low / Moderate / High / Critical.

**System pressure** (header bar) — the max-driven blend of:

- **Commit load:** committed / commit limit. This is the number that actually causes allocation failures. Weight highest; ≥90% is Critical on its own.
- **Physical load:** 1 − (available / total RAM), with `\Memory\Available MBytes` semantics (standby counts as available).
- **Paging activity:** hard fault rate (`Pages Input/sec` equivalent from fault deltas) normalized against a rolling baseline — distinguishes "RAM is full but idle" from "actively thrashing".
- **Compression signal:** Memory Compression store working set as a fraction of RAM — Windows compressing aggressively is an early-warning sign.

Score = `max(commitScore, physScore) * 0.7 + pagingScore * 0.2 + compressionScore * 0.1`, clamped; exact weights tuned during milestone 2 against real load (open 50 tabs, run a memory hog).

**Per-app pressure** (per row) — "how much is this app contributing / suffering":

- Share of total commit (contribution).
- Working-set-to-commit ratio: a low ratio under high system pressure means the app has been trimmed and will hard-fault when touched (suffering).
- Fault rate and commit growth trend over the last N intervals (leaking / ballooning).

### 4. Front-ends

**CLI (`winmempress-cli.exe`):**

- Default: one snapshot, aligned table (app, working set, commit, pressure), system pressure line at top.
- `--watch [seconds]`: redraw in place on an interval; `--json` for machine-readable output.
- Doubles as the validation harness for milestones 1–2 (diff its output against Task Manager's Apps view).

**GUI (`winmempress.exe`):** single Win32 window:

- Header area: system pressure gauge + total RAM / commit summary (owner-drawn or simple static controls).
- ListView (report view, `LVS_OWNERDATA` virtual mode): app icon + name, private working set, private commit, pressure badge (custom-draw colored cell), sorted by working set descending.
- 2s `SetTimer` refresh; collection on a worker thread posting a completed snapshot to the UI thread (`PostMessage` with heap pointer), diffed into the virtual list so sort/selection don't jump.
- Icons via `SHGetFileInfo`/`ExtractIconEx` into an `HIMAGELIST`; app display name from the exe's `FileDescription` version resource (`VerQueryValue`).

## Milestones

1. **`memcore` + CLI** — collector + grouper printing the app table; validates grouping against Task Manager's Apps view. *(No GUI risk taken until the data is right.)*
2. **Pressure engine** — system + per-app scores in the CLI output; tune thresholds under synthetic load.
3. **Win32 GUI shell** — ListView UI, icons, refresh loop over the same `memcore`.
4. **Polish** — "Background & system" aggregation accuracy, optional run-elevated relaunch, error handling for exited processes, README.

## Known risks

- Parent-PID chains lie for brokered UWP processes (all children of `svchost`/`sihost`) — package family name handles those, which is why packaged detection runs first.
- PID reuse can mis-parent a process — mitigated by start-time checks.
- Some counters need elevation for other-session processes; the unelevated fallback is the aggregate row, never a crash.
