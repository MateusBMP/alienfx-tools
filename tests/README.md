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
  alienfx_sdk/                # M2a — packet_builder_test.cpp, protocol_invariants_test.cpp
                               # M2b — transport_backend_test.cpp
  golden/alienfx_sdk/         # M2a — golden byte vectors, see below (shared by M2a and M2b)
  support/                    # M2a — transport_log (TransportKind/TransportEvent, shared by
                               #       both fakes below), fake_hid (recording transport for
                               #       hid_backend.h's Windows-shaped seam), golden_vector
                               #       (reader/writer/hex PrintTo), packet_matrix
                               #       (shared version×operation table), gen_golden
                               # M2b — fake_hidapi (recording transport for the *hidapi* C
                               #       API instead — the seam hid_backend_linux.cpp calls),
                               #       dry_run_demo (hand-run --dry-run demonstration)
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

## Golden-vector format (decided in M0, implemented in M2a)

M2a populates `tests/golden/alienfx_sdk/` for all 7 API versions without a Windows
machine, using two of the three provenance tiers below — see
`Doc/linux_roadmap/16-testing-and-validation.md` for the full rationale. The *format*
was fixed in M0, before any vector existed, so M2a wasn't also inventing conventions
while porting the HID SDK; M2a extends it (strict superset — every M0-format line is
still valid) to express transport kind and multi-packet sequences, which the four
fragile spots below specifically need. M2b (`transport_backend_test.cpp`) reuses these
same committed files unchanged — see `Doc/linux_roadmap/16-testing-and-validation.md`'s
"fake-hidapi tier" for why that's not a fourth provenance tier of its own.

### Provenance tiers

| Tier | How it's produced | Proves |
|---|---|---|
| `source-derived` | Run the **current, unmodified** packet builders against `tests/support/fake_hid.*` (a recording fake transport), freeze the output once via `tests/support/gen_golden.cpp`, commit it | The port didn't change the bytes |
| `hand-derived` | Expected bytes computed independently in the test from `alienfx-controls.h`'s constants — no dependency on any builder's output. Used in `tests/alienfx_sdk/protocol_invariants_test.cpp` for the 4 spots `Doc/linux_roadmap/16-testing-and-validation.md`/`04-alienfx-sdk-hid.md` call fragile: the V2 4-bit color packing, the V6 XOR checksum, V7's write-then-read transport order, V8's feature-vs-interrupt size heuristic | The bytes are correct, independent of the implementation |
| `hardware-captured` | Instrumented Windows build talking to real hardware (`Doc/linux_roadmap/18-windows-verification.md`) | The bytes are what the physical device wants |

A `source-derived` vector and the code under test can share a pre-existing bug — it only
proves the port is faithful to the source, not that the source is correct. When a
`hardware-captured` vector becomes available, it **replaces a `source-derived` file's
contents in place** (same path, same case name, `origin=` field updated) rather than
existing as a separate case.

### File format

- One file per case: `tests/golden/<target>/<case>.txt`.
- Contents: one or more lines, each either a bare hex line (M0 format, implies `out`) or
  a **transport-tagged line**: `> <token>  <space-separated lowercase hex bytes>`, or
  `> sleep <ms>` with no bytes. Tokens: `out` (`HidD_SetOutputReport`), `feat`
  (`HidD_SetFeature`), `write`/`read` (interrupt), `getfeat` (`HidD_GetFeature`),
  `getin` (`HidD_GetInputReport`).
- `#`-prefixed lines are comments. The **first** comment line records provenance:
  `origin` (one of the three tiers above), the source commit/gen SHA, VID/PID (when
  known), the API version, and the logical call it corresponds to (e.g. `SetMultiColor`
  V4 arm).

Example — `source-derived`, single packet (M0-compatible bare-hex form still works when
there's no sequencing or transport ambiguity to record):

```
# origin=source-derived src=<commit-sha> gen=<gen_golden-run-sha> api=v4 call=SetMultiColor/3-lights
02 03 ff 00 00 01 00 00 ...
```

Example — `hand-derived`, multi-packet with transport and timing (why the format needed
extending: this shape cannot be expressed by the M0 format at all):

```
# origin=hand-derived src=alienfx-controls.h:116-153 api=v8 call=SetBrightness
> feat   01 17 0a
> sleep 4
> sleep 6
```

Rationale: reviewable in a plain PR diff, trivially emitted by an instrumented Windows
build (dump `buffer[0..length)` before `HidD_SetFeature`/etc.) or by `gen_golden`, no
binary blobs in git, and immune to the MSVC-struct-layout questions that `CLAUDE.md`
already warns about for the registry `REG_BINARY` blobs — a golden vector is bytes on
the wire, not a memcpy'd struct.

## No CI

This project runs no CI (no `.github/workflows/`). All of the above is verified
locally, via the `dev`/`gcc`/`clang`/`gcc-fetched`/`system-only` presets in
`CMakePresets.json` — see `Doc/linux_roadmap/02-build-system.md` for when to run each.
