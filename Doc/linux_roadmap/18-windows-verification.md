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
| Build `alienfx-tools.sln`, `Release\|x64`, in VS 2026 (toolset v145); confirm green | M0 | MSVC-only toolset (`v145`); no cross-compiler target exists | See "How to verify a Windows build" below | **Open** |
| Build `alienfx-tools.sln`, `Debug\|x64`, in VS 2026 (toolset v145); confirm green | M1 | Same toolchain requirement as the M0 row, but a *different* configuration: `AlienFX_SDK.cpp:17`'s `OutputDebugString` call is `#ifdef _DEBUG`-only, so a Release-only build never compiles that line and isn't full coverage of what M1's `#ifdef _WIN32` edits touch | See "How to verify a Windows build" below | **Open** |
| Build the narrower `AlienFX-SDK/AlienFX_SDK.sln`, both configurations | M1 | Compiles the same `AlienFX_SDK.h` M1 edited, through a *different* project graph than the main solution — a project-graph-specific MSBuild issue (e.g. a missing include path) could pass the main `.sln` and fail here, or vice versa | See "How to verify a Windows build" below | **Open** |
| Runtime smoke: `alienfx-cli.exe` probe + set a color | M1 | `CustomMutex` and `ThreadHelper`'s *implementations* were rewritten behind `#ifdef _WIN32`/`#else`; a preprocessor mistake in the Windows arm (e.g. an accidentally-deleted line, a misplaced `#endif`) can compile clean and only misbehave at runtime | See "How to verify a Windows build" below, "Runtime smoke checklist" | **Open** |
| Runtime smoke: `alienfx-gui.exe` — ambient capture on/off, haptics on/off, fan tab | M1 | These are `ThreadHelper`'s actual consumers (`CaptureHelper.cpp:47,136`, `GridHelper.cpp:181-182`, `SysMonHelper.cpp:82`, `WSAudioIn.h:50`, `alienfan-tools/alienfan-mon/MonHelper.cpp:53`); nothing else in the tree exercises the rewritten code paths at runtime | See "How to verify a Windows build" below, "Runtime smoke checklist" | **Open** |
| Capture golden byte vectors: instrument `PrepareAndSend` (`AlienFX-SDK/AlienFX_SDK/AlienFX_SDK.cpp`) to dump `buffer[0..length)` before `HidD_SetFeature`/`HidD_SetOutputReport`/etc., one call per API version × command type | M2 | Needs the existing Windows build talking to real HID devices via `HidD_*`/`WriteFile` | See [16](16-testing-and-validation.md)'s golden-vector section; write output using the hex format in `tests/README.md` | **Open — largest single Windows-dependent item in the roadmap; M2 cannot be called done without it** |
| USBPcap/Wireshark capture for any device not matching [04](04-alienfx-sdk-hid.md)'s protocol tables | M2 (as needed) | Bare-metal Windows + Wireshark's `USBPcap` capture interface is the ground-truth capture path per [16](16-testing-and-validation.md) | See [16](16-testing-and-validation.md)'s USB traffic capture procedure | **Open — only needed if/when an unrecognized device shows up** |
| Dump the ACPI `{sub, arg1, arg2, 0}` command buffer via SDK v1's `RunMainCommand` escape hatch, for Backend B golden vectors | M5 | The escape hatch only exists in the Windows v1 fan SDK | See [16](16-testing-and-validation.md) and [05](05-alienfan-sdk-thermal.md) | **Open** |
| Re-confirm `alienfx-tools.sln` still builds green | Ongoing, at each milestone boundary | Same as the M0/M1 rows — this is a repeat, not a one-time check | See "How to verify a Windows build" below | **Open** |

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

## Why M0's proof-by-construction stops covering M1 onward

M0's argument above only proves something about commits that **add files without
modifying any MSBuild project input**. M1 (`e0b714a`..HEAD) is the first milestone that
doesn't fit that shape — it edits `AlienFX_SDK.h`, `Common/CustomMutex.h`,
`Common/CustomMutex.cpp`, `Common/ThreadHelper.h`, and `Common/ThreadHelper.cpp`, and all
five are enumerated in `.vcxproj`/`.vcxitems` `ClCompile`/`ClInclude` lists MSBuild
actually reads. The `git diff --stat -- '*.sln' '*.vcxproj' ...` check above still
passes (no project *file* changed) but no longer proves what it used to — it was never
claiming anything about the *contents* of files a project already references, only about
which files a project references at all.

**The replacement is weaker, and that's stated plainly rather than glossed over.** The
convention going forward: every edit to a file MSBuild compiles must be **purely
additive** — every line that existed before the edit still exists, verbatim, somewhere
in the file after it (typically inside a new `#ifdef _WIN32` arm around the
already-existing code, with the Linux implementation in the new `#else` arm). Checked
with:

```bash
git diff -U0 <last-known-good-sha> -- <path>... | grep '^-[^-]'   # expect: empty
```

(`grep '^-[^-]'` matches a real deleted content line, not the `--- a/path`/`--- b/path`
file-header lines unified diff also starts with `-`.) For M1 specifically, run against
the M0 commit and the five files it touched:

```bash
git diff -U0 e0b714a -- 'AlienFX-SDK/AlienFX_SDK/AlienFX_SDK.h' \
                        'Common/CustomMutex.h' 'Common/CustomMutex.cpp' \
                        'Common/ThreadHelper.h' 'Common/ThreadHelper.cpp' \
  | grep '^-[^-]'   # expect: empty
```

**This is an argument, not a build**, and unlike M0's it is explicitly **not airtight**:
a stray `#pragma`, an `#include` moved to the wrong side of an `#ifdef`, or a mismatched
`#ifdef`/`#endif` nesting error could all pass a "no line was deleted" check while still
changing what the *Windows* preprocessor arm sees, because none of those are themselves
deletions of an existing line — they're additions that happen to land in the wrong
place. The only thing that actually proves the Windows arm is unchanged is compiling it,
which is exactly what this file exists to track as still-open. Treat the diff check as a
fast, useful, *incomplete* local guard — not a substitute for the ledger rows above ever
actually getting performed.

## How to verify a Windows build

Written for a contributor with a Windows machine and no prior context on this repo — the
recurring "re-confirm the `.sln` still builds green" ledger row points here instead of
restating this procedure at every milestone boundary.

### Prerequisites

- **Visual Studio 2026** (or the version current when you're reading this — check
  `alienfx-tools.sln`'s `VisualStudioVersion` line if unsure) with the **C++ desktop
  development workload**, and specifically the **v145 platform toolset** and
  **Windows SDK 10.0** components (`PlatformToolset`/`WindowsTargetPlatformVersion` in
  every `.vcxproj` — see [CLAUDE.md](../../CLAUDE.md)'s build section).
- **NuGet package restore** for `alienfan-SDK` (v1) and `alienfan-low`: both pull
  `Microsoft.Windows.WDK.x64` via `packages.config`, needed only for `<acpiioct.h>`.
  Visual Studio restores this automatically on first build/open if NuGet package
  restore is enabled (Tools → NuGet Package Manager → Package Manager Settings →
  General → "Allow NuGet to download missing packages"); if it's disabled, restore
  manually via `nuget restore alienfx-tools.sln` from a Developer Command Prompt before
  building.
- A **git checkout of the exact commit being verified** — record its SHA in the Status
  column when you're done (see "Recording a result" below).

### Building both solutions, both configurations

Two solutions, two configurations each — four builds total. All from a **Developer
Command Prompt for VS 2026** (or the VS IDE, Build → Batch Build → select all four):

```bat
msbuild alienfx-tools.sln /p:Configuration=Release /p:Platform=x64
msbuild alienfx-tools.sln /p:Configuration=Debug   /p:Platform=x64
msbuild AlienFX-SDK\AlienFX_SDK.sln /p:Configuration=Release /p:Platform=x64
msbuild AlienFX-SDK\AlienFX_SDK.sln /p:Configuration=Debug   /p:Platform=x64
```

Both solutions, not just the main one: `AlienFX-SDK/AlienFX_SDK.sln` (just the light SDK
+ sample app, see [02-build-system.md](02-build-system.md)) compiles the same
`AlienFX_SDK.h` through a separate project graph with its own include paths and
preprocessor definitions — a problem specific to one graph (e.g. a missing
`AdditionalIncludeDirectories` entry) would not necessarily show up in the other. Both
configurations, not just Release: `AlienFX_SDK.cpp:17`'s `OutputDebugString` call is
`#ifdef _DEBUG`-only and is never compiled by a Release build at all.

All four must build **with zero new errors or warnings** compared to the same commit's
predecessor build. "Zero new" matters because this codebase already has pre-existing
warnings (MSVC is more permissive than GCC/Clang about several of the MS-extension
constructs the Windows-compiled files still use, like the anonymous struct-in-union
types) — don't chase those down as regressions; do flag anything that wasn't there in
the last recorded-good build.

### Runtime smoke checklist

A clean compile does not prove the `#ifdef _WIN32` arms are *correct*, only that they're
*syntactically intact* — see the airtightness caveat above. Exercise the actual rewritten
code paths:

1. **`alienfx-cli.exe`** (needs a physical Alienware/Dell G-series device, or run these
   against whatever's on hand and note if none was available): `alienfx-cli probe`,
   then set one light to a solid color, confirm it visibly changes. This exercises
   `CustomMutex` and `ThreadHelper` only insofar as `alienfx-cli` links `Common` at all —
   record whether it does before treating this as meaningful coverage of M1 specifically
   (`alienfx-gui`, below, is the real target for M1's rewritten code paths).
2. **`alienfx-gui.exe`**, each of these toggled on and confirmed actually running (not
   just "doesn't crash"):
   - Ambient capture on → observe the ambient-color light(s) actually track screen
     content; capture off → confirm the update thread actually stops (no CPU churn left
     behind, check Task Manager).
   - Haptics on/off, same pattern.
   - Fan tab: open it, confirm sensor values are live-updating (this exercises
     `alienfan-tools/alienfan-mon/MonHelper.cpp`'s `ThreadHelper` consumer).
   - Leave the GUI open for at least a minute with at least one of the above active,
     then close it normally — confirm it exits promptly (a `ThreadHelper` whose `Stop()`
     doesn't actually work would hang here, since the destructor calls `Stop()`).

### Recording a result

Fill in the Status column of the relevant ledger row(s) above with: commit SHA verified,
VS version + toolset used, date, and pass/fail (with a note on any new warning/error, or
which runtime smoke checks were and weren't possible — e.g. no physical device
available). A row stays **Open** until this is filled in; a filled-in row with a failure
noted is still more useful than leaving it Open, since it tells the next person what's
already known to be broken.
