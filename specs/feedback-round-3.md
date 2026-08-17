# Feedback round 3 — GUI polish and grouping fix

User feedback on `mempressmonitor.exe` (GUI). Five items. All changes must build clean
at `/W4 /permissive-`, no new dependencies, follow existing code style in
`src/gui/main.cpp` and `src/core/grouper.cpp`. Do NOT git commit.

## 1 + 2. Refresh loses selection and flickers — replace rebuild with in-place update

`RebuildList` in `src/gui/main.cpp` currently calls `ListView_DeleteAllItems` and
reinserts every row on every 2-second refresh. That resets scroll position, drops
selection visuals, and repaints the whole control (flicker), even though
`LVS_EX_DOUBLEBUFFER` is set.

Replace it with an in-place update:

- Sort the incoming `apps` exactly as today (background pinned last, current
  sort column/direction, name tiebreak).
- Diff against the ListView row count:
  - For rows `0..min(old,new)`: update in place. Only call `ListView_SetItemText`
    for a cell whose text actually changed (compare against the text you'd set;
    fetch the current text with `ListView_GetItemText` or, better, track the
    previously rendered strings in `DialogState` to avoid round-tripping through
    the control). Update the row's `lParam` if you keep using it.
  - If the new list is longer, insert the extra rows at the end.
  - If shorter, delete trailing rows from the end (`ListView_DeleteItem` from the
    last index down).
- Preserve selection by key exactly as today: before updating, capture the
  selected app's `key`; after updating, if that key now lives at a different row
  index, move `LVIS_SELECTED | LVIS_FOCUSED` to the new row (and clear it from
  the old one). Do NOT call `ListView_EnsureVisible` — do not yank the user's
  scroll position.
- No `WM_SETREDRAW` gymnastics should be needed once DeleteAllItems is gone; if
  a repaint glitch remains for the pressure-color column, `InvalidateRect` only
  the rows whose pressure band changed, not the whole control.
- Keep `UpdateEndTaskState` at the end.

`state.apps` must still end up holding the new sorted vector (custom draw and
End Task index into it), and it must stay in sync with the ListView rows at all
times — update `state.apps` and the rows in the same pass.

## 3. Top-6 collapsed mode with expand/collapse button

- Default view shows only the top 6 app rows (after sorting, i.e. the first 6
  rows of the sorted order) **plus** the "Background & system" row, which is
  always visible and pinned last as today. Everything else is hidden.
- Add a new button between "Always on top" and "End Task" (new control id
  `IDC_EXPAND` = 1004 in `resource.h`, add to `app.rc`). Label: `Show all` when
  collapsed, `Show top 6` when expanded. Clicking toggles the mode and refreshes
  the visible rows immediately from the already-held `state.apps` (no need to
  wait for the next capture).
- Store the flag in `DialogState` (`bool show_all = false;`).
- The collapse applies at the render layer only: `state.apps` continues to hold
  the full sorted list? **No** — keep it simple and safe: keep the *full* sorted
  list in a member (e.g. `state.all_apps`) and derive the visible subset into
  `state.apps` so all existing index-based code (custom draw, End Task,
  selection) keeps working against exactly what the ListView shows.
- If the selected app is not in the visible subset after collapsing, selection
  is simply dropped (End Task disables).
- Theme the new button like the others (`ApplyTheme` loop over control ids) and
  lay it out in `LayoutControls`: left-align it next to the "Always on top"
  checkbox with the standard gap, same height as the other button.
- Also update the dialog template in `app.rc` with a reasonable initial
  position; `LayoutControls` overrides it anyway.

## 4. Green up to 50%

In `PressureColor` (`src/gui/main.cpp`), the Moderate band (25–49) currently
renders yellow. The user wants values below 50 to read as fine. Change the GUI
mapping only (core band semantics unchanged):

- `Low` and `Moderate` → the existing green pair.
- `High` → keep the existing orange pair.
- `Critical` → keep the existing red pair.

Remove the now-unused yellow constants.

## 5. Steam attributed to Explorer when minimized to tray

In `src/core/grouper.cpp`, `root_for` walks up the parent chain until it finds a
process with a visible top-level window. When an app (e.g. Steam) minimizes to
the tray, its windows are hidden, so the walk continues up to `explorer.exe`,
and the app's memory is grouped under Explorer.

Fix: never let the walk cross the shell boundary. When examining the parent of
the current process, if the parent's exe base name is `explorer.exe`
(case-insensitive compare of `BaseName(PathOf(parent_process))`), stop the walk
and treat the **current** process as its own root (return `current`), instead of
continuing upward. Consequences:

- A tray-minimized app launched by the shell becomes its own group keyed by its
  own exe path — correct.
- Processes launched by explorer that never had windows also become their own
  groups instead of Background. That is acceptable and closer to Task Manager
  behavior.
- Explorer itself (the process with the visible shell windows) is unaffected —
  the check is on the *parent*, so explorer.exe still groups as Explorer.

Keep the existing create-time staleness check (`parent_process.create_time >
process.create_time` → background) evaluated **before** the shell check, so a
recycled PID that happens to be explorer.exe doesn't capture the process.
Actually order it: staleness check first (return background as today), then the
shell-parent check (return current).

## Build & verify

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Release
```

Must compile clean at `/W4`. Iterate until it does. Do not run the GUI; do not
git commit.
