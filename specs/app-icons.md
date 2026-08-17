# App icons in the list

Add each app's icon next to its name in the GUI ListView (`src/gui/main.cpp`
only — do not touch `src/core/`). Build clean at `/W4 /permissive-`, OS
libraries only, follow existing code style. Do NOT git commit.

## Behavior

- Column 0 shows the app's small icon to the left of the display name.
- Icon comes from the app's `exe_path` (first icon in the file).
- Fallback when the path is empty or extraction fails — including the
  "Background & system" row: the stock generic application icon.

## Implementation

- Create a small-icon image list at `WM_INITDIALOG`:
  `ImageList_Create(cx, cy, ILC_COLOR32 | ILC_MASK, 16, 16)` with
  `cx/cy = GetSystemMetricsForDpi(SM_CXSMICON/SM_CYSMICON, dpi)`, then
  `ListView_SetImageList(list, image_list, LVSIL_SMALL)`. The ListView does
  not own it; destroy it in `WM_DESTROY` (after clearing it from the control
  or just after the list is gone).
- Extract icons with `SHDefExtractIconW(path, 0, 0, nullptr, &small, MAKELONG(0, cy))`
  (shell32; add the lib to the GUI target if not already linked). Destroy the
  HICON after `ImageList_AddIcon`.
- Fallback icon: `SHGetStockIconInfo(SIID_APPLICATION, SHGSI_ICON | SHGSI_SMALLICON, ...)`;
  add it to the image list once at index 0 and reuse that index.
- Cache in `DialogState`: `std::map<std::wstring, int>` from lowercased
  `exe_path` to image-list index, so each exe is extracted once. Apps whose
  extraction fails cache the fallback index.
- Row updates: set `LVIF_IMAGE`/`iImage` when inserting rows, and in the
  in-place update path also fix `iImage` whenever the row now shows a
  different app (compare cached index; only call `ListView_SetItem` when it
  changed, same only-if-changed discipline as the text cells).
- `WM_DPICHANGED`: destroy and recreate the image list at the new metrics,
  clear the cache, re-extract lazily on the next row update, and force one
  full row refresh so images reassign.

## Build & verify

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Release
```

Iterate until clean at `/W4`. Do not run the GUI; do not git commit.
