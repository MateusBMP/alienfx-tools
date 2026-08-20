# Windows Verification Ledger

## Why this file exists

No Windows machine is available to the developer currently doing this port. Several
roadmap steps genuinely require one — an MSVC build, a USB capture with Wireshark's
USBPcap, an instrumented `PrepareAndSend` run to dump golden byte vectors — and none of
those can be self-verified from a Linux-only development environment. Rather than
letting "verify on Windows" quietly stay unverified, this file is the one place the
roadmap collects every such task, growing as later milestones surface new ones. Treat a
row here as **open** until someone with the required access performs it and fills in
the status columns; don't treat an unfilled row as passively fine.

This file is the concrete instance of the compatibility table that
[16-testing-and-validation.md](16-testing-and-validation.md) asks the port to maintain
("recording which entries have been validated against the Linux build, by whom, and on
what kernel version") — but scoped specifically to tasks that need a *Windows* machine,
as opposed to Linux hardware coverage (see
[`local/test-machine.md`](local/test-machine.md) for that side).

## Ledger

| Task | Milestone | Why Windows is required | How to perform it | Status |
|---|---|---|---|---|
| Build `alienfx-tools.sln`, `Release\|x64`, in VS 2026 (toolset v145); confirm green | M0 | MSVC-only toolset (`v145`); no cross-compiler target exists | Open the `.sln`, build `Release\|x64`, record the commit SHA and result below | **Open** |
| Capture golden byte vectors: instrument `PrepareAndSend` (`AlienFX-SDK/AlienFX_SDK/AlienFX_SDK.cpp`) to dump `buffer[0..length)` before `HidD_SetFeature`/`HidD_SetOutputReport`/etc., one call per API version × command type | M2 | Needs the existing Windows build talking to real HID devices via `HidD_*`/`WriteFile` | See [16](16-testing-and-validation.md)'s golden-vector section; write output using the hex format in `tests/README.md` | **Open — largest single Windows-dependent item in the roadmap; M2 cannot be called done without it** |
| USBPcap/Wireshark capture for any device not matching [04](04-alienfx-sdk-hid.md)'s protocol tables | M2 (as needed) | Bare-metal Windows + Wireshark's `USBPcap` capture interface is the ground-truth capture path per [16](16-testing-and-validation.md) | See [16](16-testing-and-validation.md)'s USB traffic capture procedure | **Open — only needed if/when an unrecognized device shows up** |
| Dump the ACPI `{sub, arg1, arg2, 0}` command buffer via SDK v1's `RunMainCommand` escape hatch, for Backend B golden vectors | M5 | The escape hatch only exists in the Windows v1 fan SDK | See [16](16-testing-and-validation.md) and [05](05-alienfan-sdk-thermal.md) | **Open** |
| Re-confirm `alienfx-tools.sln` still builds green | Ongoing, at each milestone boundary | Same as the M0 row — this is a repeat, not a one-time check | Same procedure as the M0 row | **Open** |

Add a row here whenever a later milestone's doc identifies a new Windows-only step,
rather than folding it into that doc's own text.

## Why a proof-by-construction argument covers M0 in the meantime

M0's own exit criterion does **not** require an actual MSVC build (see the amendment in
[17-milestones.md](17-milestones.md)) because it doesn't need one to be true. The
argument, stated honestly rather than assumed:

**Premise, verified**: `grep -rn 'Include="[^"]*\*' --include=*.vcxproj --include=*.vcxitems .`
returns nothing — there is not one wildcard item glob anywhere in the `.sln`'s project
graph. Every `ClCompile`/`ClInclude` is an explicit path.

**Conclusion**: MSBuild's build inputs for this repo are exactly the files enumerated
by that explicit graph (`.sln` → `.vcxproj`/`.vcxitems`/`packages.config`). A change
that (a) adds only new files and (b) modifies none of those project files therefore
cannot alter what MSBuild compiles, how it compiles it, or what it links — this is
stronger than a green CI build would be, since a CI build only proves one toolchain ×
one configuration combination, whereas this argument holds for all of them at once.

### The two real escape hatches to watch, going forward

1. **MSBuild auto-discovered files.** `Directory.Build.props`, `Directory.Build.targets`,
   `Directory.Packages.props`, and `Directory.Solution.props` are located by *directory
   walk*, not by project reference. Creating any of these anywhere in the tree would
   silently change the Windows build without touching a single `.vcxproj`. **Never
   create these files** as part of Linux porting work.
2. **Header shadowing.** A new file whose name collides with something on a
   `.vcxproj`'s include search path could shadow it. The names added by M0
   (`CMakeLists.txt`, `CMakePresets.json`, `cmake/*.cmake`, `tests/**`) are not C/C++
   headers and not MSBuild-magic, so they're safe — but M1+ must not casually add e.g.
   a bare `config.h` at a location that could shadow an existing Windows header.

One cosmetic, non-build effect worth knowing: a root `CMakeLists.txt` makes Visual
Studio 2026 offer CMake "Open Folder" mode for the repo. Opening `alienfx-tools.sln`
itself is completely unaffected. Do not create a `CMakeSettings.json` — that file would
be the thing that actually engages VS's CMake integration, and nothing in this roadmap
wants that.

### How the invariant is checked without automation

There is no CI in this project (a deliberate decision — see
[16](16-testing-and-validation.md)). The check is one command, run by hand before
committing anything that touches a Windows-owned path:

```bash
git diff --stat -- '*.sln' '*.vcxproj' '*.vcxitems' '*.props' '*.targets'
```

Expected output: empty. This is a **convention**, not an enforced gate — there is
nothing stopping a commit that violates it other than a person checking. If that ever
becomes a real problem, revisit adding CI specifically for this one check; until then,
per [16](16-testing-and-validation.md)'s decision, this project runs no CI at all.
