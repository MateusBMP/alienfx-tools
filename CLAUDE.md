# CLAUDE.md

Guidance for working in this repository: `alienfx-tools`, a fork of
[T-Troll/alienfx-tools](https://github.com/T-Troll/alienfx-tools) tracking upstream.

## What this repo is

Windows-only C++ toolset (~22k lines) for controlling Alienware/Dell G-series system
lights, fans and power profiles without AWCC. Component map:

| Path | What it is |
|---|---|
| `AlienFX-SDK/AlienFX_SDK/` | Low-level light SDK. USB HID protocol for API versions v2–v8, optional ACPI v0 path (1177+340 lines). |
| `alienfan-tools/alienfan-SDK/` | Fan/thermal SDK v1 — direct ACPI method calls via a third-party kernel driver (`HwAcc.sys`, loaded through `alienfan-low/`). Also hosts the ACPI light API (`AlienFan_SDK::Lights`). |
| `alienfan-tools/alienfan-SDK_v2/` | Fan/thermal SDK v2 — Windows WMI (`AWCCWmiMethodFunction` class). Selected instead of v1 unless built with `FANV1`. |
| `alienfx-cli/` | Console tool for lights (`Doc/alienfx-cli.md`). Thinnest Win32 surface of any exe here. |
| `alienfx-gui/` | Full GUI: lights, ambient (DXGI capture), haptics (WASAPI), fan/power tabs, profiles, hotkeys. |
| `alienfx-mon/` | Standalone system monitor / tray sensor tool (PDH counters). |
| `alienfan-tools/alienfan-cli/`, `alienfan-tools/alienfan-gui/` | Fan/power-only CLI and GUI. |
| `LightFX/` | Emulates Dell's `LightFX.dll` (22 `LFX_*` exports) on top of the AlienFX SDK, for LightFX-enabled games. |
| `alienfx-LFX/` | Shared-source (`.vcxitems`) client that `LoadLibrary`/`GetProcAddress`-binds `LightFX.dll` or Alienware's own. Used by `alienfx-cli`'s `highlevel` mode. |
| `Common/` | Shared Win32 helpers: tray icon, elevation, service control, update check, `CustomMutex` (SRWLOCK), `ThreadHelper` (CreateThread/CreateEvent). |
| `RegHelperLib/` | Tiny registry read helpers shared by the fan/monitor config classes. |

Protocol truth lives in data tables, not prose:
- `AlienFX-SDK/AlienFX_SDK/alienfx-controls.h` — light opcodes per API v2–v8.
- `alienfan-tools/alienfan-SDK/alienfan-controls.h` — ACPI method paths + command IDs (SDK v1).
- `alienfan-tools/alienfan-SDK_v2/alienfan-controls.h` — WMI method names (SDK v2).
- `alienfx-gui/Mappings/devices.csv` — 1885-line light-name/grid database for known hardware.

## Build

- Visual Studio (toolset `v145`, Windows SDK 10.0) via `alienfx-tools.sln`. A second,
  narrower solution exists at `AlienFX-SDK/AlienFX_SDK.sln`.
- **There is no CMake or Makefile.** Everything is MSBuild `.vcxproj`.
- `alienfan-SDK` (v1) needs the Microsoft WDK NuGet package for `<acpiioct.h>`
  (`alienfan-tools/alienfan-SDK/packages.config`); the WDK requirement does not apply
  to the rest of the tree.
- Several projects are MSBuild **shared-item** projects (`.vcxitems`), not independent
  build units — they're injected into whichever `.vcxproj` references them:
  `alienfan-tools/alienfan-shared/`, `alienfan-tools/alienfan-mon/`,
  `alienfan-tools/alienfan-curve/`, `alienfx-LFX/`.
- Libraries are linked via `#pragma comment(lib, "...")` in source (e.g.
  `AlienFX_SDK.cpp:11-12`, `Common/Common.cpp:8-9`), not via project
  `AdditionalDependencies` — grep for `#pragma comment(lib` before assuming a project's
  link inputs are visible in its `.vcxproj`.

## Key conventions

- `CharacterSet = NotSet` project-wide, so `_T()`/`TEXT()` expand to narrow (ANSI)
  strings even though `TCHAR` is used throughout — don't assume wide-string behavior.
- All persisted state lives in `HKEY_CURRENT_USER`, four independent roots:
  `SOFTWARE\Alienfx_SDK` (light/group/grid mappings), `SOFTWARE\Alienfxgui` (GUI
  settings, profiles, zones), `SOFTWARE\Alienfan` (fan curves, boosts), `SOFTWARE\Alienfxmon`
  (monitor settings). Several values are raw `memcpy`'d C structs stored as `REG_BINARY`
  — treat them as MSVC-layout-dependent, not a portable format.
- Preprocessor switches that change what gets built: `NOACPILIGHTS` (drop the ACPI light
  path, HID-only — used by `alienfx-cli` and `LightFX`), `FANV1` (select fan SDK v1
  ACPI vs. v2 WMI), `NOLIGHTS` (`alienfan-cli`), `LIGHTFX_EXPORTS`, `_SERVICE_WAY_`
  (alternate service-based driver load path, currently unused), `_TRACE_` (debug prints).
- Device detection in the light SDK is **not** a static VID/PID table — it's
  `VID + HID OutputReportByteLength` (`AlienFX_SDK.cpp:191-236`); `devices.csv` only
  supplies human-readable names/grid geometry after the fact.

## Linux port

Linux support has never shipped (see upstream discussion
[#421](https://github.com/T-Troll/alienfx-tools/discussions/421) and issues
[#255](https://github.com/T-Troll/alienfx-tools/issues/255),
[#272](https://github.com/T-Troll/alienfx-tools/issues/272),
[#434](https://github.com/T-Troll/alienfx-tools/issues/434)) — it's a maintainer
bandwidth decision, not a hard technical wall; the maintainer has repeatedly said the
SDKs are portable and asked porters to keep them as reusable libraries. This fork is
planning that port. Start at
**[`Doc/linux_roadmap/README.md`](Doc/linux_roadmap/README.md)** for the full analysis,
milestones, and per-topic design docs before writing any Linux-facing code. To refresh
those docs with new upstream/kernel/prior-art activity or reflect local implementation
progress, use the `update-linux-roadmap` skill (`.claude/skills/update-linux-roadmap/`)
rather than re-deriving the roadmap from scratch.

Hardware-specific test coverage for the machine you're working on lives in
`Doc/linux_roadmap/local/test-machine.md`. That file is intentionally **not versioned**
(ignored via `.git/info/exclude`, so it's absent on a fresh clone). **If it doesn't
exist, run the `probe-test-machine` skill (`.claude/skills/probe-test-machine/`) to
generate it** before doing Linux porting or test work — it records which HID API
version(s), fan backend, and roadmap subsystems the current machine can actually
exercise versus what must stay golden-vector-only.

## Working agreements

- Don't modify the Windows build (`.sln`/`.vcxproj`, `#pragma comment(lib` link lists,
  registry-backed config format) while doing Linux planning/porting work unless a
  roadmap doc explicitly calls for a shared change (e.g. introducing a CMake build
  alongside the existing `.sln`).
- `Doc/linux_roadmap/` documents are design artifacts, not implementation — keep code
  references in them as `path:line` so they stay checkable against the current tree.
