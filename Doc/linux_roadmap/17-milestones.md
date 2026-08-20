# Milestones: M0–M10

Each milestone lists its goal, entry/exit criteria, primary files/docs involved, a
rough size estimate (relative, based on the line counts gathered while researching
this roadmap — not a time estimate, since that depends entirely on who's available),
and risks. Milestones are ordered by dependency, but M8's three sub-areas (ambient,
haptics, hotkeys) can proceed in parallel once M7 lands, and M9 (LightFX) has no
downstream dependents at all and can slip freely.

## M0 — Build scaffolding

**Goal**: a CMake build exists alongside the `.sln`, builds a real target on Linux, and
the Windows build is unaffected by construction.
**Doc**: [02](02-build-system.md).
**Exit criteria**: `cmake --build` succeeds on Linux for at least one real (not stub)
target; no MSBuild build input (`.sln`/`.vcxproj`/`.vcxitems`/`.props`/`.targets`) is
modified — sound because no project file in the tree uses a wildcard item glob
(verified), so a new-files-only change provably cannot alter what MSBuild compiles. An
actual MSVC `.sln` build is deferred to a Windows-capable contributor and tracked in
[18-windows-verification.md](18-windows-verification.md), not required for M0 itself.
*(Originally worded as "`.sln` build unaffected" — amended because that phrasing is
unverifiable by a developer without Windows access, which makes it a criterion that
gets quietly waved through rather than actually checked.)*
**Size**: small (new files only, no logic).
**Risk**: low — mechanical, but sets conventions (option flags, dependency-acquisition
pattern, no CI, GoogleTest, `tests/` layout) every later milestone follows, so get the
shape right here rather than patching it repeatedly later.

## M1 — Platform compat layer

**Status: done.** `win_compat.h` and the CRT/threading/mutex replacements exist and
compile against the actual SDK headers that include `<wtypes.h>` today.
**Doc**: [03](03-platform-abstraction.md).
**Exit criteria — strengthened from the original milestone plan**: the original wording
("`AlienFX_SDK.h`, `Common/CustomMutex.h`, `Common/ThreadHelper.h` compile on Linux
without pulling in real Windows headers") was too weak — a header can compile while
silently getting the layout of the on-wire color/mask types wrong, which is exactly the
risk this milestone itself flagged as needing extra review. M1 shipped with that risk
mechanically checked instead of asserted:
  - `AlienFX_SDK.h`, `alienfx-controls.h`, `Common/CustomMutex.h`, `Common/ThreadHelper.h`
    compile on Linux without pulling in real Windows headers — under `-Wall -Wextra
    -Wpedantic -Werror` on both GCC and Clang (the `gcc`/`clang` presets), not just a
    bare compile.
  - The layout of all five anonymous-union types the HID protocol depends on
    (`Afx_colorcode`, `Afx_light`, `Afx_groupLight`, and the PID/VID unions on both
    `Functions` and `Afx_device`) is pinned down by `offsetof`/`sizeof` `static_assert`s
    in `tests/alienfx_sdk/sdk_headers_test.cpp`, not left to "it compiled, so it's
    probably fine."
  - `CustomMutex`'s reader/writer exclusivity and `ThreadHelper`'s do/while first-tick,
    join-on-`Stop()`, and manual-reset restart quirk are asserted by
    `tests/common/*_test.cpp`, re-run via `ctest --repeat until-fail:50` (concurrency
    tests that pass once prove little) and verified clean under AddressSanitizer and
    ThreadSanitizer by hand.
  - All of the above green under all five `CMakePresets.json` presets
    (`dev`/`gcc`/`clang`/`gcc-fetched`/`system-only`).
**Size — corrected from the original estimate**: ~80 lines of shim code was roughly
right (`win_compat.h` itself), but "`#ifdef` seams touching maybe a dozen headers" was
not — only 3 of the 8 originally-surveyed `<wtypes.h>`-including files are in M1's actual
scope (`AlienFX_SDK.h`, `Common/CustomMutex.h`, `Common/ThreadHelper.h`); the rest belong
to M4 (`RegHelperLib.h`, `ConfigMon.h`), M7 (`alienfx-gui.h`), M9 (`LFXUtil.cpp`), or are
never ported (`alienfan-SDK.h`, both copies). See
[03](03-platform-abstraction.md) for the corrected table.
**Risk — realized and addressed, not just flagged**: the anonymous struct-in-union
handling did need extra review, in a different place than expected — not the union
layout itself (GCC/Clang accept it once the right warning is silenced) but **the
silencing flag originally proposed for it was wrong** (`-Wno-microsoft-anon-tag` doesn't
match either compiler's actual warning under `-std=c++17`; see 03's corrected version).
A second, unplanned risk surfaced during implementation: `AlienFX_SDK.cpp`'s real
brace-elision call sites (`:918,999,1077`) don't compile clean under `-Werror` either,
for a related but distinct reason (`-Wmissing-field-initializers`/`-Wmissing-braces`,
not the anon-struct warning) — recorded in 03 as a flagged item for M2, since
`AlienFX_SDK.cpp` itself is out of M1's scope.

## M2 — HID light SDK port

**Split into five sub-milestones (M2a–M2e).** The milestone as originally scoped had a
single exit bar — "golden-byte-vector tests pass for all 7 versions... at least one real
device validated per version" — that bundled four problems with unrelated blocker
profiles: does the protocol code compile on Linux, did the port change any bytes, does a
Linux device get detected at all, and does it work against physical hardware. Only the
last one is genuinely blocked (no Windows machine exists to capture reference vectors,
and this fork's only test machine has one usable light controller, API_V4 — see
[local/test-machine.md](local/test-machine.md)). The original wording made the whole
milestone unable to be called done for reasons that have nothing to do with whether the
port is correct. Splitting decouples "provably didn't change the bytes" (achievable now,
on this machine, for all 7 versions) from "bytes are right for hardware" (gated on
testers/a Windows machine, tracked per-version instead of blocking everything).
**Doc**: [04](04-alienfx-sdk-hid.md), particularly its "Functions vs. Mappings" split and
the M2a–M2e map.

`AlienFX_SDK.cpp` splits cleanly along an existing class boundary: `Functions`
(`:24-890`, `:1174-1177`) is the HID protocol state machine and is what M2 ports.
`Mappings` (`:893-1172`) is registry-backed light-name/grid persistence with 37
`Reg*`/`HKEY`/`SetupDi` references — that's [M4](06-configuration-storage.md)'s scope, not
M2's, and `Functions` has zero references into it. This also means the three
brace-elision call sites M1 flagged as an M2 hand-off
(`tests/alienfx_sdk/sdk_headers_test.cpp:133-168`, `AlienFX_SDK.cpp:918,999,1077`) are all
inside `Mappings` — they move to M4's ledger, not M2's.

### M2a — Characterization baseline

**Goal**: `Functions` (the HID protocol half only — `Mappings` deferred to M4) compiles
on Linux against a recording fake transport, with byte-identical output frozen for every
packet builder across all 7 API versions, *before* any porting of the real transport or
device enumeration happens.
**Exit criteria**: `Functions` compiles under all 5 `CMakePresets.json` presets with
`-Werror`; golden vectors ([16](16-testing-and-validation.md)'s `source-derived` tier)
exist for the full version × operation matrix (~55 cases); `ctest` green; a deliberately
flipped byte in `alienfx-controls.h` makes the corresponding test fail (vectors are
load-bearing, not decorative); no line of the pre-M2a file is deleted
(`git diff -U0 c35002c -- AlienFX_SDK.cpp | grep '^-[^-]'` empty, same convention M1 used)
— `Mappings` and SetupAPI enumeration are wrapped in `#ifdef _WIN32`, not rewritten.
**Size**: ~400 lines new (`hid_backend.h` seam, `tests/support/*`, packet-builder +
protocol-invariant tests), 0 lines of `AlienFX_SDK.cpp`/`.h` deleted.
**Risk**: low — pure computation, no hardware or Windows dependency once the 6 transport
calls are factored behind `hid_backend.h`.

### M2b — hidapi transport backend

**Status: done.** `hid_backend_linux.{h,cpp}` and `tests/support/fake_hidapi.{h,cpp}`
exist; M2a's golden vectors pass unchanged through the real backend.

**Goal**: the eight Windows-shaped `hid_backend.h` symbols
(`HidD_SetOutputReport`/`SetFeature`/`GetFeature`/`GetInputReport`, `WriteFile`,
`ReadFile`, `CloseHandle`, `Sleep`) reimplemented as thin wrappers over `hidapi`, per
[04](04-alienfx-sdk-hid.md)'s mapping table — `PrepareAndSend` and all packet-construction
code stay byte-identical. Includes the `--dry-run` transport [16](16-testing-and-validation.md)
asks for.

**Exit criteria — restructured before implementation, not after.** The milestone as
originally scoped asked for "M2a's golden tests pass unchanged when driven through the
real hidapi backend **over a loopback/mock hidraw node**." That's not achievable safely:
a real loopback hidraw node needs `/dev/uhid` (root-only) or `usbip`, and a test suite
that needs root is a test suite that stops being run — the same "golden-green isn't the
same as hardware-validated" reasoning that split M2 into sub-milestones in the first
place, applied one level down. Two changes, made before writing any M2b code:

1. **M2b cannot open a device, by construction.** All device opening (`hid_open_path`,
   `hid_enumerate`, VID/PID resolution, the 33-vs-34 report-length fix) stays wholly
   M2c's. `hid_backend_linux.cpp` only ever operates on a handle someone else opened —
   verified after the fact by `nm -D` on both `alienfx_sdk_transport_tests` and
   `dry_run_demo`: neither references `hid_open`/`hid_open_path`/`hid_enumerate`/
   `hid_init` at all.
2. **The exit criterion is a fake-hidapi replay, not a loopback node.** The *real*
   backend is linked against `tests/support/fake_hidapi.cpp` (a stub of the hidapi C API,
   mirroring M2a's `fake_hid.cpp`) and driven through M2a's existing `packet_matrix`,
   asserting byte-identical output against the same committed
   `tests/golden/alienfx_sdk/*.txt` — proving the hidapi call mapping without hardware,
   root, or a real hidraw node. `alienfx_sdk_transport_tests` doesn't even link
   `libhidapi*.so` (`ldd` confirms it), so this is stronger than "doesn't touch a real
   device" — it structurally *cannot*.

Also added a safety posture the roadmap had deferred to M10: since the six wrappers are
the only place bytes can leave the process, a **vendor allowlist is enforced right
there** (the five VIDs in [15](15-packaging-and-permissions.md)'s table), refusing
`HidD_SetOutputReport`/`SetFeature`/`WriteFile` for any other vendor unless
`ALIENFX_ALLOW_ANY_VENDOR=1` is set; `ALIENFX_DRY_RUN=1` forces decode-and-print instead
of sending, for any vendor.

**Four porting defects found and fixed while implementing this milestone** (full writeup
in [04](04-alienfx-sdk-hid.md)):
1. `GetDeviceStatus`'s `Get*` calls passed an uninitialized `buffer[0]` — hidapi (and the
   underlying `HIDIOCGFEATURE`/`HIDIOCGINPUT` ioctls) read that byte as the *requested*
   report number. Fixed with three additive `#ifndef _WIN32` lines in `AlienFX_SDK.cpp`
   (`git diff -U0 aaf9d43 -- AlienFX_SDK.cpp | grep '^-[^-]'` stays empty, same M1/M2a
   convention).
2. `hid_read_timeout`'s 0-on-timeout/-1-on-error split had to be mapped onto Windows'
   `ReadFile`-returns-`TRUE`-with-zero-bytes-on-timeout contract, or V7's mandatory
   read-after-write would report failure on every call.
3. `HidD_SetOutputReport` is a control-endpoint transfer; `hid_send_output_report`
   matches it but only exists since hidapi 0.15.0 — older hidapi falls back to
   `hid_write` (a genuinely different, interrupt, transfer) at compile time, with
   `ALIENFX_HID_OUTPUT_MODE=report|write` to pick at runtime once both exist, so M2d can
   settle this against real hardware without a rebuild.
4. hidapi's own `linux/hid.c` needs `gnu11`, not `c11` (`wcsdup`/`strdup`/`strtok_r`/
   `O_CLOEXEC` are POSIX/GNU extensions with no feature-test macro of their own) — this
   project's project-wide `CMAKE_C_EXTENSIONS OFF` broke the `gcc-fetched` preset's
   from-source build until scoped back to `ON` around just the `hidapi`
   `alienfx_require_package()` call.

**Size**: ~250 lines `hid_backend_linux.{h,cpp}`, ~150 lines `fake_hidapi.{h,cpp}`,
~180 lines `transport_backend_test.cpp`, CMake `hidapi` dependency via
`alienfx_require_package()` (declared at the top-level `CMakeLists.txt`, not in
`AlienFX-SDK/AlienFX_SDK/CMakeLists.txt` — `find_package()`'s IMPORTED targets are only
visible in the calling directory and its own subdirectories, and `AlienFX-SDK/AlienFX_SDK`
and `tests/` are siblings). 3 additive lines in `AlienFX_SDK.cpp` (defect 1).
**Risk**: realized as documented above, not left implicit — mechanical once M2a's seam
existed; the CMake gotcha already flagged in `cmake/AlienfxDependency.cmake:50-54`
(hidapi's own `cmake_minimum_required` floor) did **not** fire on this hidapi version, but
the C-extensions gotcha (defect 4, unflagged going in) did.

### M2c — Linux enumeration & detection

**Goal**: replace SetupAPI device enumeration (`AlienFXProbeDevice`, `AlienFXInitialize`)
with udev/hidraw enumeration — including the actual `hid_open_path()`/`hid_enumerate()`
calls M2b deliberately left out, and a call to `alienfx_hid::RegisterDevice()`
immediately after each successful open (`hid_backend_linux.h`, M2b) so M2b's vendor
allowlist gate has a VID to check without falling back to a `hid_get_device_info()` query
per write. Fixes **Finding 1**
(`local/test-machine.md:28-54`): Linux HID report descriptors don't include the leading
report-ID byte Windows' `OutputReportByteLength` counts, so this machine's 34-byte V4
controller parses as 33 and is silently undetectable. **Fix is to normalise the parsed
Report Count to the Windows convention (+1) before the version-lookup switch — not to add
a `case 33`** (hidraw `write(2)` still expects the leading report-ID byte on the wire, so
34 is genuinely correct).
**Exit criteria**: `187c:0550` on the test machine resolves to `API_V4`; a regression test
asserts VID `0x187c` + parsed Report Count 33 → `API_V4`. First M2 sub-milestone that is
**not** purely additive to `AlienFX_SDK.cpp` — add a row to
[18-windows-verification.md](18-windows-verification.md).
**Size**: ~300 lines (udev enumeration, the report-count fix, detection tests).
**Risk**: low-medium — the fix itself is a one-line normalization, but enumeration
replacement touches code with real device-safety consequences if done carelessly.

### M2d — API_V4 live hardware validation

**Goal**: actually drive lights on real API_V4 hardware end to end.
**Exit criteria**: reset → set colour → update visibly changes lights; the 8 named
lights from `alienfx-gui/Mappings/devices.csv:127-141` individually addressable;
works non-root via group membership (pulls the udev rule design forward from
[15](15-packaging-and-permissions.md) — per `local/test-machine.md:159-164`, this is
"prerequisite work for a usable M2 exit, not later polish", since every light operation
on the test machine currently requires root).
**Size**: ~100 lines (udev rule, smoke-test script) — small because it's validation of
M2a–M2c, not new protocol code.
**Risk**: low — the risk already lives in M2a–M2c; this milestone is where it either pays
off or doesn't.

### M2e — API_V5 collection-aware detection (deferrable)

**Goal**: resolve **Finding 2** (`local/test-machine.md:56-87`) — Windows evaluates V5
detection (`OutputReportByteLength == 0 && Usage == 0xcc`) per top-level HID collection,
but Linux hidraw exposes a composite device's entire interface as one node, so a literal
port never detects V5. This needs a deliberate design decision (e.g. enumerate sibling
hidraw nodes by USB interface/collection), not a mechanical port. Also worth weighing:
`devices.csv:130` marks this machine's exact Darfon device "Unused", so "detect but don't
drive this specific model" is a legitimate resolution, not a cop-out.
**Exit criteria**: a written detection design, and either working V5 detection or an
explicit recorded decision not to drive the tested device.
**Size**: small-medium, mostly design time.
**Risk**: low — can slip indefinitely without blocking M3, since M3's "prove the chain
works" milestone only needs one working version (V4, from M2d).

### Cross-cutting: the per-version hardware ledger

V2/V3/V6/V7/V8 exit M2 code-complete and golden-green (M2a) but hardware-unvalidated —
this fork's only test machine cannot reach them at all. That's a standing state to track
continuously, not a step to complete, so it lives in
[19-hardware-validation.md](19-hardware-validation.md) rather than as an M2 sub-milestone.

## M3 — `alienfx-cli` (first usable Linux release)

**Goal**: a working, shippable Linux binary — set light colors from the command line.
**Doc**: [07](07-alienfx-cli.md). Depends on M1, M2a–M2d (M2e is not required — M3 only
needs one working API version, and M2d delivers V4), and a minimal slice of M4 (just
enough config storage for `probe`/mappings — full M4 scope not required).
**Exit criteria**: matches the acceptance bar in [07](07-alienfx-cli.md) — probe, set,
see lights change, end to end.
**Size**: ~500 lines touched.
**Risk**: low — this is deliberately the "prove the whole chain works" milestone, not
where new risk is introduced.

## M4 — Configuration storage

**Goal**: full registry→JSON config backend covering all four schemas.
**Doc**: [06](06-configuration-storage.md).
**Exit criteria**: `lights.json`/`gui.json`/`profiles.json`/`zones.json`/`fan.json`/
`monitor.json` read/write correctly, atomic-replace verified (kill-mid-write doesn't
corrupt), schema version field present.
**Size**: ~600 lines of new code (roughly matching the combined size of
`ConfigHandler.cpp` + `ConfigFan.cpp` + `ConfigMon.cpp` + `Mappings::Load/SaveMappings`
being replaced). Also owns the three brace-elision call sites M1 flagged
(`AlienFX_SDK.cpp:918,999,1077`, inside `Mappings` — see M2's doc above) since they don't
compile clean under `-Werror` and `Mappings` is this milestone's to rewrite, not M2's.
**Risk**: low-medium — mostly straightforward, but the "don't replicate binary struct
layout" discipline matters; get review on the schema before too much downstream code
depends on it.

## M5 — Fan/thermal backend + `alienfan-cli`

**Goal**: Backend A (kernel sysfs) working for allowlisted models; Backend B
(`acpi_call`) working for at least one non-allowlisted model as proof; `alienfan-cli`
shipping.
**Docs**: [05](05-alienfan-sdk-thermal.md), [08](08-alienfan-cli.md).
**Exit criteria**: matches the acceptance bar in [08](08-alienfan-cli.md) against both
backends.
**Size**: ~800 lines new code (no existing fan SDK code is reused, per
[05](05-alienfan-sdk-thermal.md)'s "clean-room" guidance).
**Risk**: highest single-milestone risk in the roadmap — kernel driver version/allowlist
gaps and ACPI method-name variance across models are both genuinely open-ended
problems, not implementation details. Budget the most schedule slack here.

## M6 — Daemon

**Goal**: `alienfxd` running as a systemd user service, D-Bus API for light/fan
state, sensor monitoring, profile persistence and auto-switching (minus foreground-app
triggers).
**Doc**: [09](09-daemon-and-monitor.md).
**Exit criteria**: matches the acceptance bar in
[09](09-daemon-and-monitor.md).
**Size**: ~1,000 lines new code, wrapping M2/M4/M5.
**Risk**: medium — mostly integration risk (making sure CLI, future GUI, and daemon
agree on ownership of device handles) rather than new protocol risk.

## M7 — Qt 6 GUI (core)

**Goal**: device/light management, visual grid, color/effect assignment, and profile
management working as a thin client over M6's daemon.
**Doc**: [10](10-gui-qt6.md).
**Exit criteria**: matches the acceptance bar in [10](10-gui-qt6.md), validated on
both an X11 and a native-Wayland session.
**Size**: largest single milestone — ~5,000–6,000 lines of new Qt code against ~9,100
lines of Windows dialog code being replaced (not 1:1, since GDI→QPainter and
`DLGPROC`→Qt widget are both compressions, not expansions).
**Risk**: high, primarily due to sheer surface area (22 dialogs) rather than any single
hard technical problem — most individual pieces are well-understood Qt patterns.

## M8 — Ambient, haptics, hotkeys (parallelizable)

**Goal**: the three GUI-adjacent effect subsystems working, each independently.
**Docs**: [11](11-ambient-capture.md), [12](12-audio-haptics.md),
[13](13-input-and-hotkeys.md).
**Exit criteria**: each doc's acceptance/priority notes.
**Size**: ~500 (ambient) + ~300 (haptics, excluding unchanged `kiss_fft`) + ~400
(hotkeys) lines new code.
**Risk**: medium-high, concentrated in Wayland portal UX friction (ambient, hotkeys)
and the `/dev/input` permission story (hotkeys) — these are the roadmap's most
compositor-dependent features; expect them to be "good enough" rather than fully
parity with Windows for some time.

## M9 — LightFX library

**Goal**: `liblightfx.so` with the 22-export API and `dlopen`-based client support in
`alienfx-cli`'s `highlevel` mode.
**Doc**: [14](14-lightfx-library.md).
**Exit criteria**: exports resolve correctly via `dlsym`, IPC signal to the daemon
works.
**Size**: ~1,100 lines touched (mostly mechanical once M1/M2/M6 exist).
**Risk**: low — low real-world Linux value means low pressure to get every edge case
right; can slip indefinitely without blocking anything else.

## M10 — Packaging

**Goal**: udev rules, kernel/module documentation, AUR + deb/rpm packages.
**Doc**: [15](15-packaging-and-permissions.md).
**Exit criteria**: a user on a supported distro can install via their package manager
and have working hidraw permissions without manual intervention beyond a
group-membership + re-login step.
**Size**: packaging scripts + udev rules, small in code volume but requires real
per-distro testing.
**Risk**: medium — permission setup is exactly the category of bug (see the issue #524
cautionary note in [15](15-packaging-and-permissions.md)) that's invisible in
development and only surfaces on a clean user machine; don't skip real clean-install
testing.

## What's not in this milestone list

[16](16-testing-and-validation.md)'s testing infrastructure (golden vectors, CI,
`--dry-run` transport) isn't its own milestone — it should be introduced starting at
M2 and maintained continuously, not bolted on at the end.
