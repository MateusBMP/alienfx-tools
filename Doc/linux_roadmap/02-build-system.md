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
- `kiss_fft` ports unchanged (pure C DSP, no Windows deps) — implemented in M0 as
  `alienfx-gui/kiss_fft/CMakeLists.txt`, `add_library(kiss_fft STATIC ...)`, reused by
  the GUI's haptics code ([12](12-audio-haptics.md)). Deliberately not gated behind
  `ALIENFX_BUILD_GUI` (see the M0 decisions above) — it's a leaf dependency, not GUI
  code, despite its directory.
- The four `.vcxitems` shared-source projects fold into whichever CMake target
  consumes them today, exactly as they do in MSBuild — do not invent new library
  boundaries for them unless a specific doc (06, 09) asks for it.
- `alienfx-LFX`'s `LoadLibrary`/`GetProcAddress` client code becomes `dlopen`/`dlsym`
  inside whichever target uses "high-level" mode (`alienfx-cli`); see
  [14](14-lightfx-library.md).

**Per-target CMake file placement — decided in M0**: `CMakeLists.txt` goes inside each
existing source directory (`AlienFX-SDK/AlienFX_SDK/CMakeLists.txt`,
`Common/CMakeLists.txt`, etc.), added via `add_subdirectory(...)` from the root, exactly
as `alienfx-gui/kiss_fft/CMakeLists.txt` already does. The alternative considered — all
per-target logic centralized in `cmake/targets/*.cmake` at repo root, landing zero new
files in upstream-owned directories — was rejected: per-directory `CMakeLists.txt` is
the idiomatic CMake layout, and critically it's *upstreamable*, which matters given the
maintainer's stated wish (see [01](01-why-no-linux-support.md)) that the SDKs stay
reusable libraries. New files conflict with upstream only if upstream happens to add
the exact same path, which is rare for a build file upstream doesn't have.

## Root `CMakeLists.txt` shape

Use per-target `if(WIN32)` / `if(UNIX)` source lists and link libraries rather than a
single global platform switch — several targets (the SDKs) will have real Linux-only
*and* Windows-only source files, not just conditional compilation inside shared files.
Gate everything Windows-specific behind `if(WIN32)` so the existing `.sln` remains the
canonical Windows build; CMake should not become the only way to build for Windows
unless a future decision explicitly retires MSBuild.

Option flags, modeled after `tr1xem/alienfx-linux`'s `ALIENFX_BUILD_CLI` pattern and
**implemented as of M0** in the root `CMakeLists.txt`:

```
option(ALIENFX_BUILD_CLI    "Build alienfx-cli / alienfan-cli" ON)
option(ALIENFX_BUILD_DAEMON "Build the background daemon"       ON)
option(ALIENFX_BUILD_GUI    "Build the Qt6 GUI"                 OFF)  # heavier deps
option(ALIENFX_BUILD_TESTS  "Build unit tests (see 16)"         OFF)  # ON via presets
option(ALIENFX_WERROR       "Treat compiler warnings as errors" OFF)  # fork-added
```

`ALIENFX_WERROR` is not part of the original four — it's a fork addition wired to the
`alienfx::warnings` INTERFACE target (`cmake/AlienfxWarnings.cmake`) so vendored code
and FetchContent'd dependencies can opt out simply by not linking it.

### M0 decisions that bind everything after it

- **CMake floor**: `cmake_minimum_required(VERSION 3.24...4.0)`. 3.24 is not arbitrary —
  it's the version that introduced `FetchContent_Declare(... FIND_PACKAGE_ARGS ...)`,
  which *is* the find-package-first/FetchContent-fallback policy below, implemented by
  CMake itself rather than a hand-rolled `if(NOT FOUND)` branch. The `...4.0` policy
  range silences CMake 4.x's old-policy nagging while still running on 3.24.
- **C++ standard**: C++17 (`CXX_STANDARD_REQUIRED ON`, `CXX_EXTENSIONS OFF`), C11 for
  vendored C. C++17 is the minimum the roadmap actually needs — [03](03-platform-abstraction.md)
  maps `CustomMutex`'s SRWLOCK onto `std::shared_mutex`, [06](06-configuration-storage.md)'s
  config backend wants `<filesystem>` — not an arbitrary pick, and not C++20 (nothing in
  the roadmap needs it). `CXX_EXTENSIONS OFF` means `-std=c++17`, not `-std=gnu++17` —
  this makes GCC and Clang *disagree* about MS-extension tolerance in more places, which
  is exactly the divergence [16](16-testing-and-validation.md) wants a GCC/Clang check
  to catch.
- **Two mechanical guards** in the root `CMakeLists.txt`: a `FATAL_ERROR` unless
  `-DALIENFX_ALLOW_WINDOWS_CMAKE=ON` when `WIN32` is set (enforces "don't drive the
  Windows build from CMake" below), and a `FATAL_ERROR` on in-source builds (keeps
  generated files out of upstream-owned directories). Both were verified to actually
  fire during implementation, and the in-source guard's droppings
  (`CMakeCache.txt`/`CMakeFiles/` at repo root, written by CMake before the guard is
  reached) needed their own `.gitignore` entries — a rejected in-source attempt still
  leaves untracked files.
- **Never glob sources.** `alienfx-gui/kiss_fft/tools/kiss_fftr.c` and
  `tools/kiss_fftr.cpp` are byte-identical (verified with `diff`); a `file(GLOB)` over
  `tools/` would compile both and produce duplicate symbols. Every target in this
  project lists its sources explicitly for this reason.
- **`kiss_fft` is the M0 proof target**, and it is deliberately **not** gated behind
  `ALIENFX_BUILD_GUI` even though it lives under `alienfx-gui/` — it's a leaf DSP
  dependency (no Windows deps, no GUI deps) consumed by the haptics code in
  [12](12-audio-haptics.md) and by the test suite. Gating it would make a default
  `cmake --build` compile nothing, defeating the point of having a first real target.
  Its `CMakeLists.txt` excludes `tools/kiss_fastfir.c` and `tools/kfc.c` (both define
  `main()` under build-time macros; `kiss_fastfir.c` also isn't warning-clean) and
  `tools/kiss_fftnd.c` (unused) — only `kiss_fft.cpp` + `tools/kiss_fftr.cpp` are built,
  matching what `alienfx-gui/WSAudioIn.cpp` actually calls.
- **`.editorconfig`/`.clang-format` are out of scope**, now and for any future
  milestone, unless explicitly revisited — a repo-wide reformat would create enormous
  merge noise against every file upstream actively edits.
- **No CI.** No `.github/workflows/` exists or is planned. See
  [16](16-testing-and-validation.md) for what replaces it.

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
often disallowed or sandboxed).

**Implemented as of M0** as `alienfx_require_package()` in `cmake/AlienfxDependency.cmake`,
on top of `FetchContent_Declare(... FIND_PACKAGE_ARGS ...)` (CMake ≥ 3.24 — the reason
for the version floor above) rather than a hand-rolled `if(NOT FOUND)` branch:

```cmake
alienfx_require_package(googletest
    FIND_ARGS      NAMES GTest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.17.0
    OPTIONS        INSTALL_GTEST=OFF BUILD_GMOCK=ON gtest_force_shared_crt=ON)
```

**Rule**: every third-party dependency in this project goes through this function.
`FETCHCONTENT_TRY_FIND_PACKAGE_MODE` defaults to `OPT_IN` — a bare `FetchContent_Declare`
without `FIND_PACKAGE_ARGS` is silently network-only and breaks distro packaging with no
error at all.

Both acquisition paths are exercised locally via `CMakePresets.json` (there is no CI to
do this automatically): the `gcc-fetched` preset sets
`FETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER` to force the source-build branch even on a
machine that has every dependency installed; the `system-only` preset sets
`FETCHCONTENT_FULLY_DISCONNECTED=ON` to simulate a distro packaging sandbox — a missing
system dependency must be a loud configure failure, never a silent network fetch. Both
were run against GoogleTest during M0 and produced the expected `STATUS` line
(`... -> built from source` / `... -> system package`) in each case.

`SYSTEM` (CMake ≥ 3.25) and `EXCLUDE_FROM_ALL` (≥ 3.28) on `FetchContent_Declare` are
used opportunistically, guarded by a `CMAKE_VERSION` check, rather than raising the
3.24 floor. Also recorded in the helper for when M2 pulls in `hidapi`/`libusb`: CMake
4.x hard-errors on a dependency declaring `cmake_minimum_required(VERSION <3.5)` —
prefer pinning a modern tag; if unavoidable, scope `CMAKE_POLICY_VERSION_MINIMUM`
(CMake ≥ 4.0) to that one call, never project-wide.

**`hidapi` landed in M2b** (`alienfx_require_package(hidapi ...)`, `hidapi-0.15.0`),
confirming two things about that gotcha and surfacing a second, unrelated one:

- The `cmake_minimum_required` floor gotcha above did **not** fire against this hidapi
  version/CMake combination — recorded as "checked", not left as an open risk.
- A gotcha nothing above predicted: hidapi's own `linux/hid.c` uses POSIX/GNU libc
  extensions (`wcsdup`/`strdup`/`strtok_r`/`O_CLOEXEC`) with no feature-test macro of its
  own, so it needs to be compiled as `gnu11`. This project's project-wide
  `CMAKE_C_EXTENSIONS OFF` (the C-standard analogue of the deliberate `CXX_EXTENSIONS
  OFF` above) is inherited by `FetchContent`'s subdirectory build unless overridden,
  which silently turns that into `c11` and hides every one of those declarations —
  caught only by actually exercising the `gcc-fetched` preset (the system-package branch
  links a prebuilt `.so` and never compiles `hid.c`, so it never surfaced there). Fixed
  the same way as the version-floor gotcha: `CMAKE_C_EXTENSIONS` toggled `ON` then back
  `OFF`, scoped tightly around just the `hidapi` `alienfx_require_package()` call.

Also worth recording since it wasn't obvious going in: `alienfx_require_package(hidapi
...)` is declared in the **top-level** `CMakeLists.txt`, not inside
`AlienFX-SDK/AlienFX_SDK/CMakeLists.txt` even though that's the only directory whose
target (`alienfx::hid_linux`) links it directly. `find_package()`'s `IMPORTED` targets
(`hidapi::include`, `hidapi::hidraw`) are only visible in the directory that called
`find_package()` and that directory's own subdirectories — `AlienFX-SDK/AlienFX_SDK` and
`tests/` (which also needs `hidapi::include` for `fake_hidapi`, and `hidapi::hidraw` for
`dry_run_demo`) are siblings, not one a child of the other, so the shallowest common
ancestor able to see the dependency in both places is the project root. This does not
apply to regular (non-`IMPORTED`) targets like `alienfx::compat`/`alienfx::warnings` —
those are visible build-tree-wide once created regardless of which directory created
them; the restriction is specific to `find_package()`'s `IMPORTED` targets.

Revisit per-dependency in [15](15-packaging-and-permissions.md) once real package
availability across target distros is known.

## What NOT to do

- Don't replace the `.sln`/`.vcxproj` files — add CMake alongside them.
- Don't try to make one `CMakeLists.txt` also drive the Windows build by wrapping
  MSBuild; keep the two build systems independent so a Linux CMake change can never
  break the Windows build accidentally.
- Don't invent a Linux-only fork of the SDK sources (rejected option — see the
  decision recorded in [README.md](README.md)); the goal is one set of `.cpp`/`.h`
  files with `#ifdef`/compat-shim seams, per [03](03-platform-abstraction.md).
