# Tests

This directory holds all Linux-port test code. It lives at the repo root rather than
nested inside a source directory (e.g. `AlienFX-SDK/AlienFX_SDK/tests/`) because **every
source directory in this tree is upstream-owned** — a top-level, fork-owned `tests/`
minimizes upstream merge surface and keeps fork-added code visually obvious.

Framework: **GoogleTest** (with GMock), acquired through `alienfx_require_package()`
(see `cmake/AlienfxDependency.cmake`) — system package first, `FetchContent` fallback.
See `Doc/linux_roadmap/02-build-system.md` and `16-testing-and-validation.md` for the
full rationale.

## Layout

```
tests/
  CMakeLists.txt              # one file; declares every test executable
  README.md                   # this file
  kiss_fft/                   # M0 (test source deferred to a follow-up pass)
  alienfx_sdk/                # M2
  golden/alienfx_sdk/         # M2 — golden byte vectors, see below
  support/                    # M2 — shared test helpers (hex loader, dry-run transport)
```

One flat `tests/CMakeLists.txt` declares every test executable — the *source* tree
needs the per-target subdirectory structure, the *build graph* doesn't.

## Adding a test suite

1. Create `tests/<target>/<name>_test.cpp`.
2. Add an `add_executable(...)` + `target_link_libraries(...)` +
   `gtest_discover_tests(...)` block to `tests/CMakeLists.txt`, following the
   commented `kiss_fft_tests` example already there.
3. Link `alienfx::warnings` (unless the target under test is vendored code that isn't
   linked against it either).

## Assertion style

- **Floating point** (e.g. `kiss_fft` results): `EXPECT_NEAR` with an explicit
  tolerance constant. Do not use exact equality on floats.
- **Integral byte buffers** (M2's packet-builder golden vectors): GMock's
  `EXPECT_THAT(actual, ElementsAreArray(expected, n))`, which reports index-level
  mismatches ("byte 7 differs") rather than dumping the whole buffer. Add a
  project-local `PrintTo(std::span<const uint8_t>, std::ostream*)` in M2 so failures
  print hex, not decimal.

## Golden-vector format (decided in M0, implemented in M2)

There are no golden vectors yet — they don't exist until M2 captures them from an
instrumented Windows build (`Doc/linux_roadmap/16-testing-and-validation.md`), and a
parser written against zero real data tends to get rewritten once real data shows up.
The *format* is fixed now so M2 isn't also inventing conventions while porting the HID
SDK:

- One file per case: `tests/golden/<target>/<case>.txt`.
- Contents: space-separated lowercase hex bytes, one or more lines.
- `#`-prefixed lines are comments. The **first** comment line records provenance:
  the Windows-build commit SHA the vector was captured from, the VID/PID, the API
  version, and the logical call it corresponds to (e.g. `SetMultiColor` V4 arm).

Example (illustrative — not a real captured vector):

```
# sha=<windows-build-commit> vid=187c pid=0550 api=v4 call=SetMultiColor
02 03 ff 00 00 01 00 00 ...
```

Rationale: reviewable in a plain PR diff, trivially emitted by an instrumented Windows
build (dump `buffer[0..length)` before `HidD_SetFeature`/etc.), no binary blobs in git,
and immune to the MSVC-struct-layout questions that `CLAUDE.md` already warns about for
the registry `REG_BINARY` blobs — a golden vector is bytes on the wire, not a memcpy'd
struct.

## No CI

This project runs no CI (no `.github/workflows/`). All of the above is verified
locally, via the `dev`/`gcc`/`clang`/`gcc-fetched`/`system-only` presets in
`CMakePresets.json` — see `Doc/linux_roadmap/02-build-system.md` for when to run each.
