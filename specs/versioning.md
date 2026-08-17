# Versioning system

Introduce a single-source version for the project and set it to **1.0.1.0**.
Build clean at `/W4 /permissive-`, no new dependencies, existing code style.
Do NOT git commit.

## Source of truth

New file `VERSION` at the repo root containing exactly `1.0.1.0` (four dotted
numbers, single line, trailing newline OK). Everything else derives from it.

## CMake (`CMakeLists.txt`)

- Read and validate the file at configure time:
  `file(STRINGS VERSION APP_VERSION LIMIT_COUNT 1)`, then fail with a clear
  `message(FATAL_ERROR ...)` if it does not match `^[0-9]+\.[0-9]+\.[0-9]+\.0$`.
  The fourth field must be literal `0`: Microsoft Store packages reserve the
  revision field (the Store rejects nonzero), and each field must be 0-65535.
- Split into four components; pass `project(MemPressMonitor VERSION <maj.min.patch> ...)`.
- Re-run configure when the file changes:
  `set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS VERSION)`.
- Provide the version to both executables as preprocessor definitions usable
  from C++ and from the resource compiler, e.g.
  `APP_VERSION_MAJOR=1 APP_VERSION_MINOR=0 APP_VERSION_PATCH=1 APP_VERSION_BUILD=0`
  and `APP_VERSION_STRING="1.0.1.0"` (mind RC quoting — define the string
  without quotes and stringize in the header, or pass it per-language; pick the
  cleanest approach that works for both cl and rc).

## Version resources

- New shared resource script `src/common/version.rc` containing a standard
  `VS_VERSION_INFO VERSIONINFO` block driven by the definitions above:
  FILEVERSION/PRODUCTVERSION from the four components; StringFileInfo (block
  `040904b0`) with CompanyName `Felix Huang`, FileDescription
  `MemPressMonitor` (GUI) — see note below, ProductName `MemPressMonitor`,
  FileVersion/ProductVersion `1.0.1.0` (from the string define),
  LegalCopyright `(c) Felix Huang`. VarFileInfo Translation `0x0409, 1200`.
- Both executables get the resource: add `src/common/version.rc` to the CLI
  target sources, and to the GUI target (either add the file to its sources or
  `#include` it from `src/gui/app.rc` — adding to sources is fine since the GUI
  has no other VERSIONINFO).
- If per-exe FileDescription (GUI `MemPressMonitor`, CLI `MemPressMonitor CLI`)
  requires two copies of the block, do NOT duplicate it — a single shared
  description `MemPressMonitor` for both is acceptable. Keep it simple.

## CLI

- `--help` output: title line becomes `MemPressMonitor CLI <version>` using the
  version string definition. No new flag needed.

## Scripts

- `tools/package-msix.ps1`: the `-Version` parameter's default becomes the
  content of the repo `VERSION` file (read + trim + validate against the
  existing `^\d+\.\d+\.\d+\.0$` pattern at runtime instead of a literal
  default). Explicit `-Version` still overrides.
- `tools/publish.ps1`: read the `VERSION` file the same way and name the
  staging dir/zip `mempressmonitor-<version>-<sha>` (e.g.
  `mempressmonitor-1.0.1.0-3f2a1b9.zip`).
- `packaging/AppxManifest.xml`: bump the committed `Version="1.0.0.0"` to
  `1.0.1.0` so the checked-in manifest matches (the script overwrites it at
  pack time regardless).

## Docs

- `CLAUDE.md`: add one line to the Build section: version lives in the root
  `VERSION` file and flows into the exes, MSIX, and publish zip from there.

## Build & verify

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S C:\source\winmempress -B C:\source\winmempress\build
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build C:\source\winmempress\build --config Release
```

Reconfigure is required since CMakeLists changes. Iterate until clean at /W4.
Verify the built exe carries the version:
`(Get-Item build\Release\mempressmonitor.exe).VersionInfo.FileVersion` must
print `1.0.1.0` (run this and confirm). Do not run the GUI; do not git commit.
