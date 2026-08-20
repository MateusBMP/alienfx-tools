# Building on Linux

This is the Linux CMake build, added alongside `alienfx-tools.sln` (the canonical
Windows build, unaffected by anything here — see
[`Doc/linux_roadmap/18-windows-verification.md`](linux_roadmap/18-windows-verification.md)
for how that invariant is maintained). Design decisions and rationale live in
[`Doc/linux_roadmap/02-build-system.md`](linux_roadmap/02-build-system.md); this doc is
just the commands.

As of this writing, the Linux port has completed Milestone M1 — the platform compat
layer. Real targets that build and are tested: the vendored `kiss_fft` DSP library
(M0), `alienfx::compat` (the `win_compat.h` header, M1), `alienfx::common`
(`CustomMutex`/`ThreadHelper`, M1), and `alienfx::sdk_headers` (`AlienFX_SDK.h` +
`alienfx-controls.h`, headers only — M2 turns this into a real static library once
`AlienFX_SDK.cpp` itself is ported). The CLIs, the daemon, and the GUI are still on the
roadmap.

## Requirements

- CMake >= 3.24, Ninja
- GCC or Clang (a C++17 / C11 compiler)
- pthreads (`Threads::Threads`, used by `alienfx::common`'s `std::thread`/
  `std::shared_mutex` backend as of M1)
- pkg-config (used by dependencies from M2 onward, e.g. `hidapi`/`libusb`)

Nothing beyond the above is required through M1 — GoogleTest is fetched automatically
if not already installed as a system package (see "Dependencies" below).

## Quick start

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Binaries land in `build/dev/bin/`, libraries in `build/dev/lib/`. `build/` is
gitignored; never build in-source (`cmake -S . -B .` is rejected by a guard in the
root `CMakeLists.txt`).

## Presets

`CMakePresets.json` defines one preset per purpose — there is no CI in this project
(see [`Doc/linux_roadmap/16-testing-and-validation.md`](linux_roadmap/16-testing-and-validation.md)
for why), so these are what a CI matrix would otherwise have run:

| Preset | Use it to... |
|---|---|
| `dev` | day-to-day build and test (Debug, default compiler) |
| `gcc` / `clang` | check both compilers agree — GCC and Clang tolerate different MSVC extensions, which matters once the ported Windows SDK code (M1+) lands |
| `gcc-fetched` | force every dependency through the FetchContent source-build path, even on a machine that has them all installed |
| `system-only` | simulate a distro packaging sandbox: no network access, system packages only |

Run any of them the same way:

```bash
cmake --preset <name> && cmake --build --preset <name> && ctest --preset <name>
```

## Local checks before committing

There is no CI. This sequence is the actual safety net — run it before committing
anything that touches build files, `cmake/`, or `tests/`:

```bash
rm -rf build/
for p in dev gcc clang gcc-fetched system-only; do
  cmake --preset "$p" && cmake --build --preset "$p" && ctest --preset "$p" || echo "FAIL $p"
done
git status --porcelain                                        # expect: empty
git diff --stat -- '*.sln' '*.vcxproj' '*.vcxitems' '*.props' '*.targets'
                                                                # expect: empty — the
                                                                # Windows build must
                                                                # never be touched by
                                                                # Linux porting work
```

If the last two commands print anything, stop and re-check — the second one especially:
see
[`Doc/linux_roadmap/18-windows-verification.md`](linux_roadmap/18-windows-verification.md)
for why it matters and what the two real exceptions to "never touched" are.

**Since M1**: some Linux porting work now edits files MSBuild also compiles
(`AlienFX_SDK.h`, `Common/CustomMutex.*`, `Common/ThreadHelper.*` so far — the milestone
docs track which ones). M0's "adds only new files" proof-by-construction doesn't cover
these; the replacement convention is that every such edit must be purely additive
(original lines preserved verbatim, new behavior wrapped in an `#ifdef _WIN32` arm), and
the check is a diff against the last commit known to build clean, expecting zero deleted
lines (a genuine `-`, not a `--` file marker or an EOF-newline artifact):

```bash
git diff -U0 <last-known-good-sha> -- 'AlienFX-SDK/AlienFX_SDK/*.h' \
                                       'Common/CustomMutex.*' \
                                       'Common/ThreadHelper.*' \
  | grep '^-[^-]'                                              # expect: empty
```

This is a documented convention, not an automated gate — consistent with this project's
no-CI stance and with how the `.vcxproj` check above already works — and it is not
airtight (a stray `#pragma`, a moved `#include`, or `#ifdef` nesting error would slip
past a line-count check). It does not replace an actual Windows build; see
[`Doc/linux_roadmap/18-windows-verification.md`](linux_roadmap/18-windows-verification.md)
for the full procedure to verify one when a Windows machine is available.

## Options

Set with `-D<NAME>=ON|OFF` on top of a preset, e.g.
`cmake --preset dev -DALIENFX_BUILD_TESTS=OFF`. Note that the `dev`/`gcc`/`clang`/
`gcc-fetched`/`system-only` presets all set `ALIENFX_BUILD_TESTS=ON` themselves, so an
override only sticks on a *fresh* binary directory, not a re-configure of an existing
one — the preset's own cache variable wins on repeat runs. To build a genuinely
tests-off configuration, use CMake directly rather than a preset:
`cmake -S . -B build/notests -G Ninja -DALIENFX_BUILD_TESTS=OFF`.

| Option | Default | Meaning |
|---|---|---|
| `ALIENFX_BUILD_CLI` | `ON` | `alienfx-cli` / `alienfan-cli` (not yet implemented — M3/M5) |
| `ALIENFX_BUILD_DAEMON` | `ON` | the background daemon (not yet implemented — M6) |
| `ALIENFX_BUILD_GUI` | `OFF` | the Qt6 GUI (not yet implemented — M7); heavier deps |
| `ALIENFX_BUILD_TESTS` | `OFF` (`ON` via presets) | the `tests/` suite |
| `ALIENFX_WERROR` | `OFF` (`ON` via `gcc`/`clang`/derived presets) | treat warnings as errors on first-party code (`alienfx::warnings`) |

## Dependencies

Every third-party dependency goes through `alienfx_require_package()`
(`cmake/AlienfxDependency.cmake`): it tries `find_package()` against a system
installation first, and falls back to fetching source via `FetchContent` only if that
fails. This matches how AUR/deb/rpm packaging expects a build to behave — see
[`Doc/linux_roadmap/02-build-system.md`](linux_roadmap/02-build-system.md).

As of M1, the only such dependency is still GoogleTest (built only when
`ALIENFX_BUILD_TESTS=ON`) — M1 added no new third-party dependencies, only first-party
targets (`alienfx::compat`, `alienfx::common`, `alienfx::sdk_headers`). Configure output
tells you which acquisition path was taken:

```
-- alienfx: dependency 'googletest' -> system package (find_package)
-- alienfx: dependency 'googletest' -> built from source (FetchContent)
```

## Windows verification

Some checks in this roadmap require an actual Windows machine and cannot be run from
here — see
[`Doc/linux_roadmap/18-windows-verification.md`](linux_roadmap/18-windows-verification.md)
for the running list and current status of each.
