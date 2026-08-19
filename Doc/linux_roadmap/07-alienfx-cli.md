# `alienfx-cli` Linux Port

This should be the **first shippable Linux binary** — it has the thinnest Win32
surface of any executable in the tree, and porting it is exactly the checklist the
maintainer gave in [issue #272](https://github.com/T-Troll/alienfx-tools/issues/272):
retype the Windows integer types, port device detection, port `PrepareAndSend`, port
the `Mappings` class off the registry.

## Current shape

`alienfx-cli/alienfx-cli.cpp` (410 lines) + `Consts.h` (75 lines). `int main(argc,
argv)` at `:105`. Console app — no dialogs, no resources, no COM. References
`AlienFX_SDK_noACPI` (i.e. builds with `NOACPILIGHTS`, pure HID) and, for `highlevel`
mode, `alienfx-LFX` (`LoadLibrary` of `LightFX.dll` or the vendor's own).

## Dependency chain to have ready first

1. [03](03-platform-abstraction.md)'s compat shim — unblocks compilation.
2. [04](04-alienfx-sdk-hid.md)'s HID port of `AlienFX_SDK` — this is what the CLI
   actually calls.
3. [06](06-configuration-storage.md)'s config backend — replaces
   `AlienFX_SDK::Mappings::LoadMappings`/`SaveMappings`
   (`AlienFX_SDK.cpp:1045,1113`, called from the CLI at `:114,370,378`).
4. [14](14-lightfx-library.md) or a stub — only needed if `highlevel`/`dlopen` mode
   ships in the same milestone; otherwise gate that command behind a "not available on
   Linux yet" message and ship `lowlevel` mode alone (see below).

## Command surface: unchanged

Keep the CLI's user-facing command set identical to `Doc/alienfx-cli.md` — that
document (44 lines, already in the repo) is the reference for exact semantics:
`status`, `setall`, `setone`, `setzone`, `setaction`, `setzoneaction`, `setpower`,
`settempo`, `setglobal`, `setdim`, `lowlevel`, `highlevel`, `loop`, `probe`. None of
these are Windows-specific in meaning — they're calls into the (now-ported)
`AlienFX_SDK` — so the port is almost entirely mechanical once steps 1–3 above are
done. Do not redesign the command syntax as part of this port; the goal is drop-in
parity so existing Windows documentation/muscle memory still applies.

## `highlevel` mode: defer or stub

`highlevel` routes through `alienfx-LFX`'s `LoadLibrary`/`GetProcAddress` binding of
`LightFX.dll` (Dell's official DLL or this project's own emulator). On Linux there is
no equivalent installed system DLL to bind to — `highlevel` only becomes meaningful
once [14](14-lightfx-library.md)'s `liblightfx.so` exists, and even then only for
niche LightFX-integrated software running on Linux (rare). Recommendation: ship
`alienfx-cli` first with `lowlevel` as the only functional mode, `highlevel` present in
the argument parser but returning a clear "not supported on Linux yet" error rather
than silently failing or being removed from the help text — keeps the command surface
identical per the constraint above while being honest about capability.

## `probe` and its interaction with the new config store

`probe[l][d][,lights][,devID[,lightID]]` (see `Doc/alienfx-cli.md:28,40-41`) is what
populates the `Alienfx_SDK` mapping data (device/light names, counts) that `setall`
etc. rely on for anything beyond raw addressing. On Windows this writes directly into
the registry via `Mappings::SaveMappings`; on Linux it should write through the new
config abstraction into `lights.json` (see [06](06-configuration-storage.md)) with the
same semantics — by default probing the first 23 lights (or 136 for keyboard devices)
per device, per `Doc/alienfx-cli.md:41`.

## `reset`

`alienfx-cli.cpp:389` sends a hardcoded V4 hard-reset packet `{0x2, 0x3, 0xff}`
directly rather than going through the general `AlienFX_SDK` reset path — preserve this
special-cased behavior exactly, it was added deliberately (see commit history: "Reset
command, fixes").

## Build target

New CMake executable target (see [02](02-build-system.md)) linking `alienfx_sdk` +
`common` + the config backend. No GUI toolkit dependency — this is the target to keep
buildable with the absolute minimum dependency set (`hidapi` + the config library only)
so it can ship even before [10](10-gui-qt6.md)'s Qt dependency is pulled in.

## Acceptance bar for this milestone

A user can: plug in a supported light device, run `alienfx-cli probe`, run `alienfx-cli
setall=255,0,0`, and see the lights turn red — end to end, no GUI, no fan control,
matching what the maintainer described as achievable "without any changes" to the CLI
logic itself once the SDK is ported.
