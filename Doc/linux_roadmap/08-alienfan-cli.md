# `alienfan-cli` Linux Port

## Current shape

`alienfan-tools/alienfan-cli/alienfan-cli.cpp` (376 lines), `int main` at `:147`.
Console app. Pulls `<combaseapi.h>` and `<PowrProf.h>` (`:5-6`,
`#pragma comment(lib,"PowrProf.lib")` at `:10`). References both fan SDK variants and
`RegHelperLib`. Documented user-facing behavior: `Doc/alienfan-cli.md` (41 lines).

## Dependency chain

1. [03](03-platform-abstraction.md) compat shim.
2. [05](05-alienfan-sdk-thermal.md)'s new `AlienFan_SDK::Control` Linux backend
   (kernel sysfs primary, `acpi_call` fallback) — this fully replaces both existing
   SDK variants; there's no "port SDK v1" or "port SDK v2" step here, just implement
   the Linux backend against the preserved public API.
3. [06](06-configuration-storage.md)'s config backend for fan curves/boost limits
   (`fan.json`).

## What changes vs. the light CLI port

Unlike `alienfx-cli` ([07](07-alienfx-cli.md)), this isn't a mechanical retype —
`alienfan-cli`'s core logic *is* built around Windows-specific concepts that don't
translate directly:

- **`PowrProf` calls** (`alienfan-cli.cpp:235-238`) — Windows power-scheme
  switching. Per [05](05-alienfan-sdk-thermal.md)'s guidance, don't build a parallel
  Linux "power scheme" abstraction; fold this into `platform_profile` switching, which
  already serves as the thermal-profile control on Linux and is the closest local
  analogue to what AWCC/Windows power plans do.
- **Privilege model differs.** On Windows, `alienfan-cli`/`alienfan-gui` "always
  require Administrator rights" (per the root `README.md`'s Security section) because
  the driver-backed ACPI access needs it. On Linux:
  - Backend A (kernel `alienware-wmi` sysfs): `fan*_boost` writes need root *unless* a
    udev rule relaxes hwmon write permissions for a specific group — decide and
    document this in [15](15-packaging-and-permissions.md), don't silently require
    `sudo` for every invocation if a udev rule can avoid it.
  - Backend B (`acpi_call`): `/proc/acpi/call` writes are root-only by default; there's
    no equivalent udev-permission workaround since it's a `/proc` interface, not a
    device node. Document that Backend B effectively requires running as root or via a
    small privileged helper (see [09](09-daemon-and-monitor.md)'s daemon/polkit note).

## Command surface

Keep `Doc/alienfan-cli.md`'s documented commands and semantics as the target — same
principle as [07](07-alienfx-cli.md): this is a port, not a redesign. Re-map any
command that assumes a Windows power-scheme GUID to the `platform_profile` string
values instead ([05](05-alienfan-sdk-thermal.md) lists them: `custom`, `balanced`,
`quiet`, `balanced-performance`, `performance`, `cool`, `low-power`).

## Build target

New CMake executable, links the Linux `alienfan_sdk` + `common` + config backend. No
GUI/Qt dependency, same minimal-footprint goal as `alienfx-cli`.

## Acceptance bar for this milestone

A user can: run `alienfan-cli` to read current fan RPM/temperature, switch thermal
profile, and (where the backend supports it) set a manual fan boost — end to end,
against either backend, with a clear error message (not a crash or silent no-op) when
neither backend is available on their hardware.
