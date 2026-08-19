# Build System: MSBuild → CMake

## Current state

`alienfx-tools.sln` (repo root) lists 19 real entries plus solution folders:

- Static/shared libs: `AlienFX_SDK`, `AlienFX_SDK_noACPI`, `alienfan-SDK`,
  `alienfan-SDK_V2`, `alienfan-low`, `Common`, `RegHelperLib`, `LightFX`, `kiss_fft`
  (vendored, under `alienfx-gui/kiss_fft/`).
- Executables: `alienfx-cli`, `alienfx-gui`, `alienfx-mon`, `alienfan-cli`, `alienfan-gui`.
- **Shared-item projects (`.vcxitems`), not independent build units** — injected by
  reference into whichever `.vcxproj` uses them: `alienfx-LFX`, `alienfan-tools/alienfan-shared`
  (`ConfigFan`), `alienfan-tools/alienfan-curve` (`FanCurve.cpp`),
  `alienfan-tools/alienfan-mon` (`MonHelper.cpp`).
- Legacy installer: `Install/Install.vdproj` (VS Setup project, out of scope for CMake).

A second, narrower solution exists at `AlienFX-SDK/AlienFX_SDK.sln` (just the light SDK
+ sample app) — keep both building if touched.

Toolchain settings are uniform: `PlatformToolset = v145`,
`WindowsTargetPlatformVersion = 10.0`, `CharacterSet = NotSet`. `alienfan-SDK` (v1) and
`alienfan-low` pull the `Microsoft.Windows.WDK.x64` NuGet package (see their
`packages.config`) purely for `<acpiioct.h>` — this dependency is Windows-only and
disappears entirely on the Linux side (Linux fan control doesn't need it — see
[05](05-alienfan-sdk-thermal.md)).

**Nothing in the tree links via `.vcxproj` `AdditionalDependencies` except**
`AlienFX_SDK.vcxproj` / `AlienFX_SDK_noACPI.vcxproj` (which list `hid.lib`,
`setupapi.lib`, plus boilerplate `advapi32`/`user32`/etc.). Every other library
dependency is a `#pragma comment(lib, "...")` embedded in source:

| Lib | Source location |
|---|---|
| `setupapi.lib`, `hid.lib` | `AlienFX_SDK.cpp:11-12`, `alienfan-low.c:6` |
| `wbemuuid.lib` | `alienfan-tools/alienfan-SDK_v2/alienfan-SDK.cpp:6`, `alienfx-gui/SysMonHelper.cpp:7` |
| `PowrProf.lib` | `alienfan-cli.cpp:10`, `alienfan-gui.cpp:12`, `alienfx-gui/FanDialog.cpp:6` |
| `Wininet.lib`, `Version.lib` | `Common/Common.cpp:8-9` |
| `dxgi.lib`, `d3d11.lib` | `alienfx-gui/DXGIManager.hpp:32-33` |
| `comctl32.lib`, `msimg32.lib` | `alienfx-gui/alienfx-gui.cpp:13-14` |
| `pdh.lib` | `alienfx-mon/SenMonHelper.cpp:4` |

This means a CMake port must **grep source for `#pragma comment(lib` per target**
rather than trusting `.vcxproj` files to enumerate link inputs.

## Target plan

Mirror the existing solution's dependency shape, not its project types. Proposed CMake
target graph (library targets are `STATIC` unless noted):

```
alienfx_sdk          (AlienFX-SDK/AlienFX_SDK — NOACPILIGHTS by default on Linux;
                       ACPI variant only meaningful if 05's ACPI fallback ships)
alienfan_sdk         (new Linux implementation — see 05; replaces both
                       alienfan-SDK and alienfan-SDK_v2, no alienfan-low equivalent)
common               (Common/ — platform-abstracted per 03)
reg_helper           (RegHelperLib/ — becomes part of the config backend, see 06)
lightfx              (LightFX/ → liblightfx.so, see 14)

alienfx-cli           (exe, depends on alienfx_sdk + common)
alienfan-cli          (exe, depends on alienfan_sdk + common)
alienfxd              (new: daemon, depends on alienfx_sdk + alienfan_sdk + common, see 09)
alienfx-gui            (exe, Qt6, depends on alienfx_sdk + alienfan_sdk + common, see 10)
```

Notes:
- `kiss_fft` ports unchanged (pure C DSP, no Windows deps) — becomes a plain
  `add_library(kiss_fft STATIC ...)` reused by the GUI's haptics code ([12](12-audio-haptics.md)).
- The four `.vcxitems` shared-source projects fold into whichever CMake target
  consumes them today, exactly as they do in MSBuild — do not invent new library
  boundaries for them unless a specific doc (06, 09) asks for it.
- `alienfx-LFX`'s `LoadLibrary`/`GetProcAddress` client code becomes `dlopen`/`dlsym`
  inside whichever target uses "high-level" mode (`alienfx-cli`); see
  [14](14-lightfx-library.md).

## Root `CMakeLists.txt` shape

Use per-target `if(WIN32)` / `if(UNIX)` source lists and link libraries rather than a
single global platform switch — several targets (the SDKs) will have real Linux-only
*and* Windows-only source files, not just conditional compilation inside shared files.
Gate everything Windows-specific behind `if(WIN32)` so the existing `.sln` remains the
canonical Windows build; CMake should not become the only way to build for Windows
unless a future decision explicitly retires MSBuild.

Suggested option flags, modeled after `tr1xem/alienfx-linux`'s `ALIENFX_BUILD_CLI`
pattern:

```
option(ALIENFX_BUILD_CLI    "Build alienfx-cli / alienfan-cli" ON)
option(ALIENFX_BUILD_DAEMON "Build the background daemon"       ON)
option(ALIENFX_BUILD_GUI    "Build the Qt6 GUI"                 OFF)  # heavier deps
option(ALIENFX_BUILD_TESTS  "Build unit tests (see 16)"         OFF)
```

## Dependency acquisition

Linux-side third-party deps needed across the roadmap: `hidapi` (with libusb or hidraw
backend — pick one per [04](04-alienfx-sdk-hid.md)), `libusb-1.0`, `Qt6` (Widgets +
maybe DBus, [10](10-gui-qt6.md)), `libevdev` ([13](13-input-and-hotkeys.md)), PipeWire
client libs ([11](11-ambient-capture.md), [12](12-audio-haptics.md)), a JSON or TOML
library ([06](06-configuration-storage.md)).

`tr1xem/alienfx-linux`'s `AlienFX-SDK/CMakeLists.txt` fetches `libusb-cmake`, `hidapi`,
`loguru`, and `nlohmann_json` entirely via `FetchContent` at configure time. That's fine
for a hobby build but is a poor fit for actual distro packaging (AUR/deb/rpm expect
`find_package`-able system libraries, and network access during a packaged build is
often disallowed or sandboxed). Prefer:

```cmake
find_package(hidapi QUIET)
if(NOT hidapi_FOUND)
    include(FetchContent)
    FetchContent_Declare(hidapi ...)
    FetchContent_MakeAvailable(hidapi)
endif()
```

for each dependency — `find_package` first, `FetchContent` fallback for
developer/CI convenience. Revisit per-dependency in [15](15-packaging-and-permissions.md)
once real package availability across target distros is known.

## What NOT to do

- Don't replace the `.sln`/`.vcxproj` files — add CMake alongside them.
- Don't try to make one `CMakeLists.txt` also drive the Windows build by wrapping
  MSBuild; keep the two build systems independent so a Linux CMake change can never
  break the Windows build accidentally.
- Don't invent a Linux-only fork of the SDK sources (rejected option — see the
  decision recorded in [README.md](README.md)); the goal is one set of `.cpp`/`.h`
  files with `#ifdef`/compat-shim seams, per [03](03-platform-abstraction.md).
