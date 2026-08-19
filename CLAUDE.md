# MemPressMonitor

Small pure-Win32 (no .NET) C++20 app showing per-app memory usage and a heuristic memory-pressure score. Architecture, source layout, heuristic, and milestones are in `PLAN.md` — read it before changing code.

## Structure

- `src/core/` — `memcore` static lib (collector, grouper, pressure engine). No UI, no console output, no `<commctrl.h>` here.
- `src/cli/` — `mempressmonitor-cli.exe`, table/JSON front-end and the validation harness.
- `src/gui/` — `mempressmonitor.exe`, Win32 ListView front-end (milestone 3).
- `packaging/` — MSIX manifest for the Microsoft Store. `Assets/` and `out/` are generated and gitignored.

## Store packaging

`tools\package-msix.ps1` builds Release and packs the GUI into an MSIX. Store
identity values are script parameters, never committed. `store.md` has the
submission answers and listing copy. Assets come from
`tools\generate-store-assets.ps1`.

## Build

CMake + MSVC (VS 18 Community installed). cmake.exe: `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`

```powershell
cmake -S . -B build-x64 -A x64
cmake --build build-x64 --config Release
```

`build-x64` is the single build tree, shared by `tools\publish.ps1` and
`tools\package-msix.ps1` (which adds `build-arm64` for ARM packages).

Version lives in the root `VERSION` file and flows into the exes, MSIX, and publish zip from there.

Must compile clean at `/W4 /permissive-`. No third-party dependencies — OS libraries only.

## Delegating implementation to Codex

Implementation work is delegated to the OpenAI Codex CLI using the **GPT-5.6 Luna model at high reasoning effort**, via the `luna` profile (`~/.codex/luna.config.toml`: `model = "gpt-5.6-luna"`, `model_reasoning_effort = "high"`):

```powershell
codex.exe exec -p luna -s workspace-write -C C:\source\winmempress "<detailed prompt>"
```

- `codex.exe` is at `C:\Users\yuehu\AppData\Local\Programs\OpenAI\Codex\bin\codex.exe`.
- Use `-s read-only` for analysis-only runs; `workspace-write` when it should edit files.
- Give Codex a self-contained prompt referencing `PLAN.md`; tell it to build and iterate until clean, and **not** to git commit.
- Claude's role: write the spec/prompt, review the diff, build, test, and commit.

## Conventions

- Unicode builds (`UNICODE`/`_UNICODE`), RAII wrappers for HANDLEs, no dead code, no TODO comments.
- `memcore` fills structs (`appmodel.h`); front-ends render them.
