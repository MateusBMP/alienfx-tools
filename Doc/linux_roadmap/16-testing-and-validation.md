# Testing and Validation

## Starting point: no test suite exists

There is no unit or integration test infrastructure anywhere in this repository today
— no test project in `alienfx-tools.sln`, no test framework dependency. The Linux port
is a natural point to introduce testing discipline, both because new code is being
written from scratch (the config backend, the fan backend) and because the existing
protocol logic being reused ([04](04-alienfx-sdk-hid.md)) is exactly the kind of
byte-layout code that benefits most from golden-value tests.

## Framework, layout, and format — decided in M0

This doc originally left the test framework, directory layout, and golden-vector format
unspecified. M0 decided and implemented all three (`cmake/AlienfxDependency.cmake`,
`tests/CMakeLists.txt`, `tests/README.md` — the latter is the authoritative source for
the details below, kept in one place rather than duplicated here):

- **Framework: GoogleTest** (with GMock), acquired via `alienfx_require_package()`
  (see [02](02-build-system.md)). Chosen over Catch2/doctest because
  `gtest_discover_tests()` lives in CMake's own builtin `GoogleTest.cmake` module, so
  CTest discovery works identically whether GTest came from the system package or
  FetchContent — Catch2's equivalent ships inside its own source tree and needs
  mode-dependent `CMAKE_MODULE_PATH` handling, which is exactly what the dependency
  helper exists to avoid. CMake also ships `FindGTest.cmake` in the box (exporting
  `GTest::gtest`/`gtest_main`/`gmock`/`gmock_main`), so `find_package(GTest)` succeeds
  even on distros whose GTest package omits its own CMake config.
- **Layout: top-level `tests/`**, not nested inside a source directory — every source
  directory in this tree is upstream-owned, so a fork-owned top-level directory
  minimizes merge surface. Sub-structured by target (`tests/kiss_fft/`,
  `tests/alienfx_sdk/`, `tests/golden/<target>/`, `tests/support/`) with one flat
  `tests/CMakeLists.txt` declaring every test executable.
- **Golden-vector format**: one file per case at `tests/golden/<target>/<case>.txt` —
  space-separated lowercase hex bytes, `#`-comments, first comment line recording
  provenance. M2a extends this (see `tests/README.md`, the authoritative copy) with an
  optional leading transport token per line (`out`/`feat`/`write`/`read`/`getfeat`/
  `getin`/`sleep <ms>`) as a strict superset — a bare hex line with no token is still
  valid — because the bytes-only format cannot express a multi-packet sequence
  (`SavePowerBlock` emits 6) or which transport call a packet went through, and two of
  the four fragile spots ([04](04-alienfx-sdk-hid.md)'s V7 write-then-read and V8
  feature-vs-interrupt heuristic) are specifically about *that*. The provenance comment
  also grows an `origin=` field (`source-derived`/`hand-derived`/`hardware-captured`,
  see below) alongside the existing SHA/VID/PID/version/call fields.
- **Assertion style**: `EXPECT_NEAR` for float comparisons (e.g. FFT results);
  `EXPECT_THAT(actual, ElementsAreArray(expected, n))` for integral byte buffers, which
  reports index-level mismatches rather than dumping the whole buffer.

## Packet-builder unit tests using golden byte vectors

`AlienFX_SDK`'s offset-patch model ([04](04-alienfx-sdk-hid.md)) makes this cheap:
every command is `template bytes + a list of (offset, bytes) patches`, and
`PrepareAndSend` deterministically produces a final buffer from those inputs. This
validates the port without needing hardware for every test run, and pins down the exact
behaviors flagged as fragile in [04](04-alienfx-sdk-hid.md) (the V8
feature-vs-interrupt size heuristic, the V6 XOR checksum, the V2 4-bit color packing,
V7's write-then-read).

### Vector provenance: three tiers, not one

This doc originally specified a single source for golden vectors — an instrumented
Windows build dumping `buffer[0..length)` before `HidD_SetFeature`/etc. That source is
real (see [18-windows-verification.md](18-windows-verification.md)) but **permanently
unavailable** to a Linux-only developer, and gating M2 on it stalls the entire milestone
indefinitely for no reason connected to whether the port is correct — see
[17-milestones.md](17-milestones.md)'s M2 split. Packet construction is pure
computation with no hardware or Windows dependency once the six transport calls are
factored behind a seam (`hid_backend.h`, M2a), so two more sources of ground truth are
available *today*, and all three now coexist as tiers with different guarantees:

| Tier | Source | Proves | Available |
|---|---|---|---|
| `source-derived` | Run the **current, unmodified** packet builders on Linux against a recording fake transport (M2a's `tests/support/fake_hid.*` + `gen_golden`), freeze the output once, commit it | The port did not change the bytes | Now, all 7 API versions |
| `hand-derived` | Expected bytes computed independently in the test itself, from `alienfx-controls.h`'s constants and this doc's formulas — no dependency on any builder's output | The bytes are correct, independent of the implementation | Now, for the 4 fragile spots named above (`tests/alienfx_sdk/protocol_invariants_test.cpp`) |
| `hardware-captured` | Instrumented Windows build talking to real hardware | The bytes are what the physical device actually wants | Whenever a Windows-capable contributor performs the ledger row in [18](18-windows-verification.md) |

Say the limitation plainly rather than gloss over it: a `source-derived` vector and the
code under test can share a pre-existing bug in `AlienFX_SDK.cpp`/`alienfx-controls.h` —
it proves "the port is faithful to the source", never "the source is correct". That's
exactly why the `hand-derived` tier exists for the four spots most likely to hide a real
bug, and why `hardware-captured` is designed as an **upgrade that replaces a vector file
in place** (same path, same case name) rather than a separate thing M2 additionally
waits on — nothing about the test structure changes when a hardware capture becomes
available, only the file's provenance comment and its contents.

### The recording fake transport (M2a)

`tests/support/fake_hid.{h,cpp}` implements the same six symbols `hid_backend.h`
declares (`HidD_SetOutputReport`/`SetFeature`/`GetFeature`, `WriteFile`, `ReadFile`,
`HidD_GetInputReport`), plus `Sleep`, but instead of talking to a device it appends each
call to an ordered log of `{transport kind, bytes, length}` (or a `sleep <ms>` entry) and
returns success. Because `PrepareAndSend` (`AlienFX_SDK.cpp:95-146`) is the single choke
point every packet goes through, this one fake is sufficient to observe every builder's
output for every API version without a real `hid_device*` or root access. A second
requirement it must meet: `Reset()` on API v1–v4 polls `GetDeviceStatus()` via
`WaitForReady`/`WaitForBusy` (`AlienFX_SDK.cpp:822-852`, up to 100–500 iterations) — the
fake returns a ready status on the first poll so the test suite stays fast, not because
polling behavior itself is being tested here.

`tests/support/gen_golden.cpp` is a small executable, run by hand and **not** wired into
`ctest`, that drives every case in the shared `packet_matrix` table against the fake and
writes `tests/golden/alienfx_sdk/<case>.txt`. Golden vectors are committed artifacts —
running `gen_golden` again should reproduce them byte-for-byte (an empty `git diff`),
proving the generator is deterministic; it is not part of the build or test loop.

Do the same three-tier approach for [05](05-alienfan-sdk-thermal.md)'s Backend B ACPI
call encoding (the 4-byte `{sub, arg1, arg2, 0}` buffer) — golden values are easy to
produce since the existing v1 SDK's `RunMainCommand` escape hatch can dump the exact
buffer it sends for known operations, once a Windows machine is available.

## A `--dry-run` transport for manual testing without hardware

Add a build-time or runtime option where the HID/ACPI transport layer prints the
packet it *would* send (hex dump, with a human-readable decode of known fields) instead
of writing to the device. This is useful independent of the golden-vector tests above:
it lets a contributor without the specific hardware model in question sanity-check
that their change produces a plausible packet, and lets a hardware owner without dev
tooling capture what a specific CLI invocation actually sends for a bug report.

## Hardware validation matrix

`alienfx-gui/Mappings/devices.csv` ([04](04-alienfx-sdk-hid.md),
[06](06-configuration-storage.md)) already encodes 28 known machine configurations and
~22 distinct VID/PID pairs across 5 vendors — treat this as the hardware coverage
matrix for release validation. It won't be feasible to physically test every entry
before every release; instead:

- Maintain a living compatibility table recording which entries have been validated
  against the Linux build, by whom, and on what kernel version (relevant for the
  fan-backend allowlist question in [05](05-alienfan-sdk-thermal.md)). This is now a
  concrete document rather than an aspiration:
  [19-hardware-validation.md](19-hardware-validation.md) is the light-SDK instance of
  it (the Linux counterpart to [18](18-windows-verification.md)'s Windows-side ledger);
  `local/test-machine.md` is that table's first populated entry, per its own header.
- Prioritize validation coverage across API *versions* (v2 through v8) over exhaustive
  per-model coverage within a version — a working V4 implementation on one m-series
  notebook is strong evidence for every other V4 device, but doesn't validate V5's
  feature-report-only detection quirk or V7's write-then-read requirement.
- Recruit testers the way upstream issue #434 organically did — contributors who
  already own specific hardware and are motivated to get their own machine working are
  the realistic source of coverage, not the roadmap author personally acquiring 28
  laptops.

## USB traffic capture procedure for new/unclear devices

For any device where the existing protocol tables in
[04](04-alienfx-sdk-hid.md)/[05](05-alienfan-sdk-thermal.md) don't obviously apply
(new hardware released after this roadmap was written, or an ambiguous VID/PID not in
`devices.csv`):

1. On the Windows build: capture USB traffic with Wireshark's `USBPcap` capture
   interface, or `usbmon` if dual-booting Linux and running the *existing Windows*
   AWCC/this-tool's Windows build under a VM with USB passthrough (less reliable —
   prefer bare-metal Windows + Wireshark/USBPcap for ground truth).
2. On the Linux side once a candidate implementation exists: `usbmon` (via
   `cat /sys/kernel/debug/usb/usbmon/0u` or Wireshark's native Linux USB capture
   support) to compare actual outgoing packets against the golden vectors from Windows.
3. For ACPI-path fan control specifically, use T-Troll's
   [`rwdec`](https://github.com/T-Troll/rwdec) against a BIOS/ACPI dump (`acpidump`
   on Linux, `RWEverything` on Windows) to find the model's `WMAX` method mapping,
   exactly as documented in [05](05-alienfan-sdk-thermal.md) and demonstrated live in
   upstream issue #434's exchange between `urbanze` and `T-Troll`.

## CI: deliberately none

**This project runs no CI** — no `.github/workflows/`, decided explicitly during M0.
Every check below is a local, developer-run command instead. This is a real tradeoff,
recorded honestly rather than glossed over: these checks now depend on someone actually
running them before a commit, with nothing automated catching a skipped check.

What would have been CI jobs are `CMakePresets.json` presets instead, each one command:

| Preset | Replaces the CI job that would have... |
|---|---|
| `gcc`, `clang` | ...built the Linux target with both compilers, catching the MSVC-extension issues cataloged in [03](03-platform-abstraction.md) — anonymous struct-in-union, calling conventions — since GCC and Clang differ in which MS extensions they tolerate and how. In M0 this only compiles portable vendored C (`kiss_fft`); it starts paying off in M1, once Windows-flavored code first compiles under both. |
| `gcc-fetched` | ...kept the FetchContent dependency-acquisition branch exercised, instead of it silently rotting between milestones. |
| `system-only` | ...caught a dependency that only works via network fetch, before a packager hit the same failure. |

Confirming the existing MSVC `.sln` build still succeeds — this roadmap's explicit
constraint that Linux work must not break Windows
([README.md](README.md)'s decisions, [02](02-build-system.md)'s "what NOT to do") — is
**not** a CI job either. See [18-windows-verification.md](18-windows-verification.md)
for the full argument: M0's new-files-only shape makes this provable by construction
without an actual Windows build (no `.vcxproj`/`.vcxitems` in the tree uses a wildcard
item glob, verified), and that same doc is where an actual MSVC build gets tracked as
an open item for whenever a Windows-capable contributor is available.
