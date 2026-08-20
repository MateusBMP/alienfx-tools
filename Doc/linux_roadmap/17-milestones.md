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

**Goal**: `AlienFX_SDK` builds and runs on Linux, controlling real hardware for at
least one device per API version (v2–v8).
**Doc**: [04](04-alienfx-sdk-hid.md).
**Exit criteria**: golden-byte-vector tests ([16](16-testing-and-validation.md)) pass
for all 7 versions' command builders; at least one real device validated per version.
**Size**: ~1,500 lines touched (`AlienFX_SDK.cpp`/`.h`, new hidapi shim layer per the
`tr1xem/alienfx-linux` technique), `alienfx-controls.h` unchanged.
**Risk**: medium — the V8 feature/interrupt heuristic and V7's write-then-read
requirement are the two spots most likely to need real-hardware iteration; budget
extra validation time for these two versions specifically.

## M3 — `alienfx-cli` (first usable Linux release)

**Goal**: a working, shippable Linux binary — set light colors from the command line.
**Doc**: [07](07-alienfx-cli.md). Depends on M1, M2, and a minimal slice of M4 (just
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
being replaced).
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
