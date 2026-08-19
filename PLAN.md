# MemPressMonitor — Plan

A small Windows app that shows **apps** (not raw processes), their total memory usage, and a **memory pressure** score. Windows has no native "memory pressure" metric, so we define a heuristic.

## Tech stack

- **C++20, pure Win32** — no .NET, no external frameworks. UI is a plain Win32 window with a ListView common control (comctl32 v6 via manifest); everything links against OS-provided libraries only (`kernel32`, `user32`, `comctl32`, `psapi`, `ntdll`, `shell32`, `version`).
- **CMake + MSVC** build producing three targets from one tree:
  - `memcore` — static library: collector, grouper, pressure engine. No UI or console dependencies.
  - `mempressmonitor-cli.exe` — console front-end over `memcore` (table output, optional watch mode).
  - `mempressmonitor.exe` — Win32 GUI front-end over `memcore`.
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
2. **Desktop apps — window anchors:** a process that owns a visible, unowned top-level window (`EnumWindows` + `GetWindowThreadProcessId` + `IsWindowVisible` + no owner) *anchors* an app. A windowless process belongs to the app of its **nearest visible ancestor** (walking parents with the create-time PID-reuse guard and a cycle guard); a windowless process with no visible ancestor goes to Background & OS — including windowless brokers such as `steam.exe`, whose UI lives in child processes. A visible process launched by another visible process is deliberately its **own app** (a game launched by Steam), matching the taskbar's one-button-per-app perception. Rationale recorded after testing every OS-exposed alternative: window titles, start-time proximity, job objects (identity is unreadable), ProductName (Valve is inconsistent), CompanyName (over-merges publishers), directory containment (games live inside the Steam directory), and AppUserModelID (unset in practice) all fail; the visible-window anchor is the only deterministic boundary the OS actually exposes.
3. **Same-exe merge:** anchors with identical exe paths merge (multiple Notepad windows = one "Notepad" row; Chromium child processes share the browser's exe and therefore its row).
4. **Everything else** (services, background processes) collapses into one "Background & OS" row so totals still add up. Kernel pool (paged + non-paged, from `GetPerformanceInfo`) is charged to this row too — it belongs to no process, and folding it in keeps e.g. a leaking driver visible. Because of this fold, per-process pool quotas must never be surfaced as columns (double count).

Shared-segment GPU bytes are already inside each process's commit charge, and the resident portion is inside its working set. The gpu_shared value is therefore an informational overlay: it is never added into the working-set or commit columns.

App identity for display: exe `FileDescription` (fallback: exe name), icon extracted from the exe / package logo.

**Shared memory amortization.** A per-app row's memory is Σ private working set plus, per distinct exe path in the group, the *largest* shared working set (`working_set_size − working_set_private_size`) among that exe's processes. Rationale: same-exe siblings (Chromium-style helpers) map the same images, so their shared pages overlap almost entirely — summing shared double-counts, ignoring it hides framework DLLs. The goal is a ballpark app footprint, not an exact number; system DLLs counted once per app are accepted as rounding error. Commit is *not* amortized: shared code charges no commit, so Σ private commit is already exact. All inputs come from the single `NtQuerySystemInformation` pass — no per-process handle opening for this.

### 3. Memory pressure heuristic

The pressure score answers one question: **is memory slowing this app down?** Every score is 0–100 and maps to four bands:

- **Low (green, 0–24):** memory is not slowing this app.
- **Moderate (yellow, 25–49):** memory is slowing it measurably but below human-noticeable levels. This also covers a latent trim: working set below half of commit while system memory is tight, so the app will fault when the user returns to it.
- **High (orange, 50–74):** noticeably affected right now but not sustained, likely a spike such as paging back in after an app switch.
- **Critical (red, 75–100):** noticeably affected and sustained across at least three intervals.

The collector uses `HardFaultCount` from `SYSTEM_PROCESS_INFORMATION`. Hard-fault rates are deltas between successive snapshots divided by QPC elapsed seconds; the first snapshot has a zero rate. Per-app history stores the last hard-fault count and the consecutive interval count at or above 100 hard faults/sec.

**Per-app pressure** uses `kMeasurableFaultsPerSec = 10.0`, `kNoticeableFaultsPerSec = 100.0`, and `kSustainedIntervals = 3`:

1. Rate ≥ noticeable with at least three consecutive intervals is Critical.
2. Rate ≥ noticeable otherwise is High.
3. Rate ≥ measurable is Moderate.
4. A latent trim is Moderate: working-set / commit < 0.5, commit > 0, and `max(commit_ratio, physical_ratio) ≥ 0.8`.
5. Otherwise it is Low.

Scores are linearly interpolated within the selected band. Low maps 0–10 faults/sec to 0–24; Moderate maps 10–100 to 25–49; a latent-trim-only score is `25 + (1 − working_set / commit) × 24`; High maps 100–1000 to 50–74; and Critical maps 100–1000 to 75–100. All ranges are clamped. Commit-share and commit-growth terms are not part of the score.

**System pressure** uses `kSystemMeasurableFaultsPerSec = 50.0`, `kSystemNoticeableFaultsPerSec = 500.0`, and the same sustained interval count. Its memory load is `max(commit_total / commit_limit, 1 − physical_available / physical_total)`, with zero-denominator guards. Load score is `memory_load / 0.9 × 100`, clamped. Fault score maps 0–5000 hard faults/sec to 0–100. The raw score is `0.5 × load_score + 0.5 × fault_score`.

The system band comes from the raw score, except a rate at or above the system noticeable threshold forces at least High. Critical requires both a raw score of at least 75 and three sustained noticeable-fault intervals; otherwise a raw score of at least 75 is capped at High. The final numeric score is clamped into the selected band’s range. There is no rolling baseline.

### 4. Front-ends

**CLI (`mempressmonitor-cli.exe`):**

- Default: one snapshot, aligned table (app, working set, commit, pressure), system pressure line at top.
- `--watch [seconds]`: redraw in place on an interval; `--json` for machine-readable output.
- Doubles as the validation harness for milestones 1–2 (diff its output against Task Manager's Apps view).

**GUI (`mempressmonitor.exe`):** single Win32 window:

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
