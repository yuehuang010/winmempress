# Milestone 1 spec — memcore + CLI

Implement the `memcore` static library and the CLI front-end per `PLAN.md` (read it first). Do NOT create the GUI target yet.

## Constraints

- C++20, pure Win32, Unicode builds (`UNICODE`/`_UNICODE`). No third-party dependencies. MSVC via CMake (minimum version 3.20).
- Layout exactly as PLAN.md: `CMakeLists.txt` at root, `src/core/{appmodel.h,snapshot.h,snapshot.cpp,grouper.h,grouper.cpp,pressure.h,pressure.cpp}`, `src/cli/main.cpp`.
- Targets: static lib `memcore`, executable `winmempress-cli`. Compile with `/W4 /permissive-`. Link psapi, shell32, version, kernel32, user32; resolve `NtQuerySystemInformation` from ntdll at runtime or link ntdll directly.
- Code quality: clean, obvious, no cleverness, no dead code, no TODO comments. RAII wrappers for HANDLEs. `src/core/` must not include `<commctrl.h>` and must not print anything.

## snapshot

- One `NtQuerySystemInformation(SystemProcessInformation)` pass for: PID, parent PID, image name, working set, pagefile usage (private commit), page fault count, session id, create time.
- Then best-effort `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` per PID for `QueryFullProcessImageNameW` and `GetPackageFamilyName` (kernel32, Win8+; load dynamically). Processes that cannot be opened are still kept with the info from the system pass.

## grouper (per PLAN.md)

1. Packaged apps grouped by package family name.
2. Desktop app roots = processes owning a visible unowned top-level window (`EnumWindows` + `GetWindowThreadProcessId` + `IsWindowVisible` + `GetWindow(GW_OWNER) == null`); descendants folded in via parent-PID chain with create-time check to guard against PID reuse.
3. Roots with the same exe path merged.
4. Everything else into one "Background & system" entry.
- Display name: `FileDescription` from the exe version resource, fallback exe base name.

## pressure (per PLAN.md)

- System score: commit load (`GetPerformanceInfo` commit total/limit), physical load (`GlobalMemoryStatusEx`), page-fault delta rate vs a rolling baseline, Memory Compression process working-set fraction. Blend formula is in PLAN.md.
- Per-app score: commit share, WS/commit ratio under system pressure, fault-rate and commit growth trend.
- Both 0–100 with Low/Moderate/High/Critical bands.
- A `MemPressEngine` class holds history and is fed successive snapshots; trend metrics need at least 2 samples and must degrade gracefully on the first.

## CLI

- No args: take two snapshots 500 ms apart (so deltas exist), print a system pressure line, then an aligned table (App, WorkingSet, Commit, Pressure) sorted by working set descending, sizes human-readable (MB/GB).
- `--watch [seconds]` (default 2): redraw in place until Ctrl+C.
- `--json`: machine-readable JSON instead of the table.
- `--help`.
- Console output must be Unicode-safe (`_setmode` + `wprintf`, or `WriteConsoleW`).

## Definition of done

- Builds clean at /W4: `cmake -S . -B build` then `cmake --build build --config Release` (VS 18 and CMake installed; cmake.exe at `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`).
- Run the CLI once and sanity-check the output before finishing.
- Do not git commit; leave the working tree for review.
