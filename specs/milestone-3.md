# Milestone 3 spec — Win32 GUI (mempressmonitor.exe)

Add the GUI front-end over the existing `memcore` library. Read `PLAN.md` and `CLAUDE.md` first; do not modify `src/core/` or `src/cli/` except the root `CMakeLists.txt` to add the new target.

## Shape

A dialog-style app, not a full document window:

- `WinMain` + a dialog resource template instantiated as the main window (`DialogBoxParam` or `CreateDialogParam` + message loop — either is fine, keep it simple).
- Fixed-size dialog frame: caption, system menu, minimize box; no maximize, no resize.
- Title: "MemPressMonitor".
- Contents, top to bottom:
  1. A single ListView (report view) filling most of the dialog.
  2. One button at the bottom right: **"End Task"**.

## ListView

- Columns: **App** (left, wide), **Memory** (right-aligned, human-readable private working set, MB/GB), **Pressure** (right-aligned, shown as a percent, e.g. `42%`).
- Rows = `AppEntry` results from `memcore` (`CaptureSnapshot` → `GroupProcesses` → `MemPressEngine::Update`), sorted by working set descending.
- Refresh every 2 seconds via `SetTimer`. Collection runs on the UI thread is acceptable at this scale ONLY if a capture stays well under ~100 ms; otherwise use a worker thread posting results back. Measure once and choose.
- Preserve selection across refreshes: key rows by `AppEntry::key`, re-select the previously selected key after rebuild (or update rows in place).
- Pressure cell color-coded via `NM_CUSTOMDRAW` (subitem stage): band Low → green, Moderate → yellow/amber, High and Critical → red. Color the text (bold-readable on the default background) or the cell background with contrasting text — pick one and make it legible.
- Full-row select, no gridlines needed, `LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER`.

## End Task button

- Disabled when nothing is selected or when the "Background & system" row is selected.
- On click: confirmation `MessageBoxW` ("End <app name>? This will terminate N process(es).", Yes/No, warning icon). On Yes: `OpenProcess(PROCESS_TERMINATE)` + `TerminateProcess` on every PID in the selected `AppEntry::process_ids`; ignore individual failures (process may already be gone), refresh immediately after.

## Windows theme

The app must match the current Windows app theme (light/dark), including the title bar:

- Detect the theme from `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize` value `AppsUseLightTheme`.
- Title bar / border: `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)` (attribute 20; fall back to 19 on failure for older builds). Link `dwmapi`.
- Dialog background: `WM_CTLCOLORDLG` / `WM_CTLCOLORSTATIC` / `WM_CTLCOLORBTN` returning a theme brush (dark: ~RGB(32,32,32) with white text; light: system defaults).
- ListView: `ListView_SetBkColor`, `ListView_SetTextBkColor`, `ListView_SetTextColor` per theme; apply `SetWindowTheme(listview, L"DarkMode_Explorer", nullptr)` in dark mode (and `L"Explorer"` in light) so the scrollbar and header follow. Link `uxtheme`.
- Re-apply live on `WM_SETTINGCHANGE` with lParam string "ImmersiveColorSet".
- Pressure green/yellow/red colors must stay legible in both themes (pick darker shades on light background, brighter shades on dark).

## Plumbing

- New CMake target `mempressmonitor` (WIN32 executable) from `src/gui/`, linking `memcore` and `comctl32`.
- `app.manifest`: comctl32 v6 dependency + per-monitor-v2 DPI awareness; `app.rc` includes the manifest and the dialog template.
- Same flags as the other targets: `/W4 /permissive- /EHsc`, `UNICODE`/`_UNICODE`.
- Files: `src/gui/main.cpp` (+ `src/gui/app.rc`, `src/gui/app.manifest`, `src/gui/resource.h`; split a `listview.cpp` out only if `main.cpp` gets unwieldy).

## Definition of done

- Whole project builds clean at /W4: `cmake -S . -B build` then `cmake --build build --config Release` (cmake at `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`).
- `mempressmonitor-cli` still builds and runs unchanged.
- Launch `build\Release\mempressmonitor.exe` briefly to confirm it starts, then close it programmatically (e.g. start it, `Start-Sleep 3`, `Stop-Process`) — do not leave it running.
- Do not git commit; leave the working tree for review.
