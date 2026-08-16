# Microsoft Store submission

Everything needed to publish this app to the Microsoft Store: how to build the
package, and prepared answers for every question Partner Center asks.

---

## 0. Reserve the name and get your publisher identity

The product name is **MemPressMonitor**. It deliberately contains neither
"Windows" nor "Win": Store policy forbids product names that imply a Microsoft
affiliation, and a name leading with "Windows" is very likely to be rejected at
name reservation or certification.

If you ever change the name, change it in all three places and keep them
identical:

- `src/gui/app.rc` — the `CAPTION` on `IDD_MAIN`
- `src/gui/app.manifest` — `assemblyIdentity/@name`
- `packaging/AppxManifest.xml` — `Properties/DisplayName` and
  `uap:VisualElements/@DisplayName`

**Your publisher identity.** Reserve the name in Partner Center first
(**Apps and games → New product → MSIX or PWA**). Then open
**Product management → Product identity** and copy these three values:

| Partner Center field | Passed to the packaging script as |
| --- | --- |
| Package/Identity/Name | `-IdentityName` |
| Package/Identity/Publisher | `-Publisher` |
| Package/Properties/PublisherDisplayName | `-PublisherDisplayName` |

They are *not* committed to this repo — `packaging/AppxManifest.xml` ships
development placeholders that the script overwrites in the staged copy.

---

## 1. Build the package

```powershell
pwsh.exe -NoProfile -File tools\package-msix.ps1 -IdentityName "<Identity/Name>" -Publisher "CN=<publisher GUID>" -PublisherDisplayName "<Your display name>" -Version 1.0.0.0
```

Output: `packaging\out\MemPressMonitor-1.0.0.0-x64.msix`.

Notes:

- The Store re-signs whatever you upload, so submission packages are left
  unsigned. Add `-Sign` only for a local sideload test — it mints a self-signed
  certificate and prints the two commands needed to trust it.
- The version's fourth component must be `0`; the Store reserves it. Bump the
  third component for each resubmission — the Store rejects a version it has
  already seen.
- ARM64 is a separate package: rerun with `-Architecture arm64` and upload both
  `.msix` files to the same submission. This needs the MSVC ARM64 build tools
  installed. Skipping ARM64 is fine — ARM64 devices run the x64 build under
  emulation.
- Only the GUI (`mempressmonitor.exe`) is packaged. The CLI is a development and
  validation harness with no Start menu entry, so it is excluded.

### Before you upload

Run the Windows App Certification Kit against the package. It catches most of
what Store certification would fail you on, and takes a few minutes:

```powershell
& "C:\Program Files (x86)\Windows Kits\10\App Certification Kit\appcert.exe" reset
```

Then launch **Windows App Cert Kit** from the Start menu, choose *Validate a
Windows app package*, and point it at the `.msix`.

---

## 2. Partner Center answers

### Account

You need a Microsoft **Partner Center developer account** before anything else
(one-time registration fee; individual and company tiers differ in price and in
how long identity verification takes). A company account requires a verifiable
business identity — for a solo project, register as an individual.

### Pricing and availability

| Question | Answer |
| --- | --- |
| Base price | **Free** |
| Free trial | Not applicable |
| Markets | **All markets** — the app has no network access, no regional content, and no export-controlled cryptography |
| Visibility | **Public** — available in the Store and discoverable in search |
| Schedule | Release **as soon as it passes certification** |
| Device families | **Windows 10/11 Desktop only.** Uncheck Xbox, HoloLens, and Surface Hub — the app reads desktop process memory counters and has no meaning on those devices |
| Organizational licensing | Allow — *Make my product available to organizations with Store-managed (online) volume licensing* |

### Properties

| Question | Answer |
| --- | --- |
| Category | **Utilities & tools** |
| Subcategory | **Backup and manage** |
| Secondary category | None |
| Does this product access, collect, or transmit personal information? | **No** |
| Privacy policy URL | Required by Partner Center even when no data is collected. Publish the statement in section 4 below at a stable URL (a GitHub Pages page or a repo file works) and enter it here |
| Website | Your GitHub repository URL |
| Support contact info | An email address you monitor, or the repository's Issues URL |
| Minimum hardware | None. Do not check any of the graphics/memory/touch requirements |
| Product declarations — *This app allows users to make purchases* | No |
| Product declarations — *This app has been tested for accessibility* | **No.** Only check this if you have actually validated keyboard navigation and screen-reader behavior; a false claim here is a certification failure |
| Product declarations — *This app depends on non-Microsoft drivers or services* | **No** |
| Product declarations — *This app can transmit or receive data over the internet* | **No** |
| System requirements | Windows 10 version 2004 (build 19041) or later |

### Age ratings (IARC questionnaire)

The app has no violence, sexuality, profanity, gambling, drug references,
horror, or user-generated content. Answer **No** to every content question. It
also has **no** user interaction, location sharing, digital purchases, or
advertising. The result is the lowest rating in every region (ESRB **Everyone**,
PEGI **3**).

### Restricted capability declaration

The manifest declares one restricted capability, `runFullTrust`. Partner Center
asks you to justify restricted capabilities in the submission notes:

> This is an unmanaged C++ Win32 desktop application packaged as MSIX.
> `runFullTrust` is the standard capability every packaged desktop application
> declares in order to run as a full-trust Win32 process; the app requires no
> other restricted capability. Full trust is what allows it to call
> `OpenProcess`, `GetProcessMemoryInfo`, and `NtQuerySystemInformation` to read
> the memory counters of running processes, which is the app's sole function.

`runFullTrust` is granted automatically for packaged desktop applications — it
does not require a separate approval request the way capabilities such as
`packageQuery` or `broadFileSystemAccess` do.

### Notes for certification

Paste this into **Submission options → Notes for certification**. It preempts
the two things a reviewer will flag:

> **Test account:** none required. The app has no sign-in, no accounts, and no
> network access of any kind.
>
> **How to test:** launch the app. It immediately lists the running apps on the
> machine with their private working set, private commit, and a memory-pressure
> rating, refreshing every two seconds. Open a few memory-heavy applications and
> the list reorders. No other setup is needed.
>
> **Why it enumerates other processes:** the app is a memory monitor, the same
> category as Task Manager's Processes tab. It reads memory counters
> (`NtQuerySystemInformation` for the system-wide process list, `OpenProcess`
> with `PROCESS_QUERY_LIMITED_INFORMATION` plus `GetProcessMemoryInfo` for
> per-process detail) and reads nothing else from those processes — no memory
> contents, no command lines, no injection, no hooking. Processes it cannot open
> unelevated are aggregated into a single "Background & system" row rather than
> being retried with elevation. The app runs unelevated and never requests
> elevation.
>
> **The End Task button:** the app can terminate a process, exactly as Task
> Manager does. It acts only on the row the user has explicitly selected and
> only when the user clicks the button; it never terminates anything on its own,
> on a schedule, or in the background, and it does not target any specific
> vendor's software.
>
> **Data:** the app stores nothing on disk, writes no registry values, creates
> no background tasks or services, and makes no network connections. It reads
> one registry value (the system light/dark theme preference) to match the OS
> theme.
>
> **Third-party code:** none. The app links only against Windows OS libraries
> (kernel32, user32, comctl32, psapi, ntdll, shell32, version, dwmapi, uxtheme).

---

## 3. Store listing copy

### Product name

> MemPressMonitor

### Short description (max 1000 characters; shown in search results)

> See which apps are eating your memory — and whether memory is actually slowing
> them down.

### Description (max 10,000 characters)

> MemPressMonitor shows you your PC's memory the way you actually think about it: by
> app, not by process.
>
> Task Manager tells you how much memory an app is using. It does not tell you
> whether that matters. MemPressMonitor adds the missing half of the picture — a
> memory pressure rating that answers a single question: is memory slowing this
> app down right now?
>
> **Apps, not processes.** A browser with forty tabs is one row, not forty. Child
> processes are folded into the app that owns them, packaged apps are grouped by
> package, and everything left over is collapsed into a single background row so
> the numbers still add up.
>
> **A pressure rating you can act on.** Every app gets a Low, Moderate, High, or
> Critical rating derived from its hard page-fault rate and how much of its
> memory the system has trimmed away. Low means memory is not your problem.
> Critical means this app is being slowed by memory right now, and has been for
> several seconds running.
>
> **System pressure at a glance.** A single score combines how much of your
> committed memory and physical RAM is in use with how hard the system is
> faulting, so you can tell "lots of RAM in use" apart from "actually short on
> RAM."
>
> **Small, fast, and quiet.** Written in plain C++ against the Windows API, with
> no frameworks and no third-party dependencies. It refreshes every two seconds
> without noticeably adding to the load it is measuring.
>
> **It does not phone home.** No accounts, no telemetry, no network access at
> all. Nothing is written to disk. The app runs without administrator rights.
>
> **Also:** end a runaway task directly from the list, keep the window above your
> other windows while you watch, and get a light or dark UI matching your Windows
> theme automatically.
>
> Requires Windows 10 version 2004 or later.

### What's new in this version

> First release.

### Product features (up to 20 short bullets)

> - Memory usage grouped by app, not by process
> - Memory pressure rating per app: Low, Moderate, High, or Critical
> - System-wide memory pressure score
> - Private working set and private commit for every app
> - Refreshes automatically every two seconds
> - End a runaway task from the list
> - Always-on-top mode
> - Automatic light and dark theme
> - No network access, no telemetry, no accounts
> - Runs without administrator rights

### Search terms (up to 7, 30 characters each)

> memory monitor, ram usage, memory pressure, task manager, system monitor,
> page faults, performance

### Copyright and trademark info

> © 2026 Yue Huang. Licensed under the MIT License; the full text is included
> with the app.

### Screenshots (required — at least one)

Capture the running app at **1366 × 768** or **3840 × 2160**, saved as PNG,
under 50 MB each. Up to 10 are allowed; three or four is a good listing:

1. The main window with a full app list and a mix of pressure ratings.
2. The window under memory load, with at least one app rated High or Critical.
3. The dark theme.

Do not include a mocked-up device frame, a price, or any Microsoft logo — the
Store rejects those.

### Store logo

Optional; the Store falls back to the package's `StoreLogo.png`, which
`tools/generate-store-assets.ps1` already produces. If you want a dedicated
one, supply a 300 × 300 PNG.

---

## 4. Privacy statement

The Store requires a privacy policy URL. Publish this text somewhere stable and
link it:

> **MemPressMonitor privacy policy**
>
> MemPressMonitor does not collect, store, or transmit any personal information.
>
> The app has no network capability. It makes no network connections of any
> kind, contains no telemetry, no analytics, and no crash reporting, and has no
> accounts or sign-in.
>
> The app reads memory usage counters for the processes running on your device
> in order to display them to you. This information is held in memory only, is
> shown only on your own screen, and is discarded when the app closes. Nothing
> is written to disk.
>
> The app reads one Windows setting — your light/dark theme preference — so that
> its window matches the rest of the system.
>
> Questions: <your support email>.

---

## 5. Submission checklist

- [ ] Developer account registered and identity verified
- [ ] Product name **MemPressMonitor** reserved in Partner Center
- [ ] Identity values copied from **Product management → Product identity**
- [ ] Package built with those identity values, at a version not previously submitted
- [ ] Windows App Certification Kit run against the `.msix` and passing
- [ ] Package sideloaded and launched from the Start menu on a clean machine
- [ ] Screenshots captured at 1366 × 768 or larger
- [ ] Privacy policy published at a public URL
- [ ] Notes for certification pasted into the submission
- [ ] Age rating questionnaire completed
