# Why Linux Isn't Supported Today

## It's a bandwidth decision, not a technical dead end

The maintainer (T-Troll) has answered "will you support Linux" the same way at least
three times over two years, and each time has (a) declined to do it himself and
(b) explained it's feasible and offered to help a contributor do it.

**[Discussion #421](https://github.com/T-Troll/alienfx-tools/discussions/421)**
(2024-03-07), asked directly "will you add linux support for fan control?":

> "I use Linux for my main duty, but WSL enough for me (and i keep W10 as a main). So
> no, Linux support is not planned."
>
> "However, for fan control, v2 version utilize WMI calls, it's Windows-specific (but
> some Linux distro have emulation)."

**[Issue #255](https://github.com/T-Troll/alienfx-tools/issues/255)** (2022-11-29),
"Any Plans for Linux Support?":

> "I only use Linux as WSL, so i can't do it. Clone repo and port the app yourself -
> it's quite easy for CLI. All source code is available."

**[Issue #272](https://github.com/T-Troll/alienfx-tools/issues/272)** (2023-01-02),
"cli for linux?" — the maintainer gave a concrete porting checklist:

> "What you need to do:
> - I use Windows types (like `WORD`, `DWORD`). It needs to be mapped to Linux ones.
> - Modify detection functions (`AlienFXCheckDevice`, `AlienFXInitialize`) to scan HID
>   Linux way.
> - Modify `PrepareAndSend` to use Linux USB IOCTRLs, not Windows.
>
> After this step, basic functions like "set one light"/"set one light effect" will
> work. But for mass-updates, you also need to convert `Mappings` class - it's using
> registry now, and needs to be altered to `/etc` config file - mass-updates require
> light ID's list."

And later in the same thread, in response to a user wanting to add support to OpenRGB:

> "Feel free to use my SDK - check how i detect various Alienware chips (BTW, it's not
> only Alienware) and interfaces will be the same (but keep in mind - some controlled
> by report, some by feature, some by direct data!). AW keyboards can have both
> Alienware and Chicony IDs (or even Darfon one for internal notebook RGB keyboards),
> it's listed into `alienfx-controls.h`."

**[Issue #434](https://github.com/T-Troll/alienfx-tools/issues/434)** (2024-07-08 →
2025-11-01, still open) is the most substantial thread — it's effectively a running
design discussion between the maintainer and several would-be porters
(`JL2210`, `urbanze`, `kuu-rt`, `tr1xem`). Key maintainer statements:

> "All you need to port my SDKs (Fan and FX). Now it uses Windows APIs, you need to
> alter it to Linux one. CLI will work after without any changes. Most of the lights
> are USB HID devices, so it's simple. Fans more tricky. SDKv2 utilize proprietary Dell
> WMI functions... But SDKv1 uses direct ACPI methods call, so it's easy to port as
> well. The only issue, ACPI method names different from model to model, so you need to
> dump you BIOS and check."

> "This port doesn't need to develop any drivers, you just need to be familiar with USB
> HID (using some lib or directly) and some skill working with Linux ACPI device."

> "HID interfaces uses all 3 types of communications - Report, Feature_set, Interrupt.
> You should support it all."

> "I recommend to keep SDKs as library, so you can reuse it into different cli/gui."

> "You can port mine SDK (just change device detection from Windows to Libusb). In this
> case, we can share new API and mods with easy."

The recurring theme: **the maintainer will not personally build a Linux port** (no
Linux dev environment, no motivation — WSL is enough for his own use), but has never
refused a community port and has actively supported would-be porters with protocol
documentation, ACPI mapping notes, and his own decoding tool
([`rwdec`](https://github.com/T-Troll/rwdec)).

## The real technical blockers

Four things actually stand between this codebase and Linux, in order of how much they
constrain the architecture:

1. **Fan/thermal control has no Linux equivalent in either existing SDK.**
   - SDK v1 (`alienfan-tools/alienfan-SDK/`) evaluates ACPI methods through a
     proprietary signed Windows kernel driver (`HwAcc.sys`, loaded via
     `alienfan-low/`) that exposes a generic ring-0 port/PCI/MSR/physical-memory
     backdoor (`Ioctl.h:249-294`) — nothing like it should exist on Linux, and nothing
     like it is needed there either; Linux has its own (safer) ACPI method-call paths.
   - SDK v2 (`alienfan-tools/alienfan-SDK_v2/`) goes through COM/WMI
     (`IWbemServices::ExecMethod` against the `AWCCWmiMethodFunction` class) — WMI is
     a Windows-specific management stack with no Linux runtime.
   - Full protocol detail and the Linux replacement plan: [05](05-alienfan-sdk-thermal.md).

2. **All persisted configuration is Windows registry, and much of it is raw struct
   dumps.** Four `HKEY_CURRENT_USER` roots (`Alienfx_SDK`, `Alienfxgui`, `Alienfan`,
   `Alienfxmon`); several `REG_BINARY` values are `memcpy`'d MSVC-layout C structs
   (e.g. `alienfx-gui/ConfigHandler.cpp:431-474`), not a portable format. See
   [06](06-configuration-storage.md).

3. **The GUI is ~9,100 lines of Win32 `DLGPROC` state-machine code** across 22 dialog
   templates (`alienfx-gui/alienfx-gui.rc`, 839 lines), plus DXGI screen capture
   (`DXGIManager.cpp/.hpp`, ATL-dependent), WASAPI loopback audio (`WSAudioIn.cpp`),
   and low-level keyboard/mouse hooks (`WH_KEYBOARD_LL`/`WH_MOUSE_LL`). None of this
   has a 1:1 Linux/Wayland equivalent. See [10](10-gui-qt6.md)–[13](13-input-and-hotkeys.md).

4. **The build is MSBuild-only** with MSVC-specific CRT usage throughout (`sscanf_s`
   ×27, `strcpy_s` ×9, `sprintf_s` ×5), `TCHAR`/`_T()`, and `#pragma comment(lib, ...)`
   link directives embedded in source rather than expressed in a portable build
   system. See [02](02-build-system.md) and [03](03-platform-abstraction.md).

By contrast, the thing every "is this even possible" question keeps coming back to —
**USB HID light control** — is *not* a blocker. It's plain HID report/feature/interrupt
traffic (`AlienFX-SDK/AlienFX_SDK/AlienFX_SDK.cpp`), and the maintainer has said so
repeatedly. See [04](04-alienfx-sdk-hid.md).

## What's changed since the maintainer's last "not planned" (2024)

The Linux side of this problem has moved since the answers above, which meaningfully
improves the fan-control story that used to be the hardest blocker:

- **`alienware-wmi-wmax` kernel driver** (merged for Linux 6.13, with manual fan-boost
  support following) exposes exactly the functionality SDK v2 gets from WMI, natively,
  through sysfs: `/sys/class/platform-profile/` for thermal profiles and
  `/sys/class/hwmon/*` (driver name `alienware_wmi`) for fan RPM/boost and temperature
  sensors. Documented at
  [docs.kernel.org/admin-guide/laptops/alienware-wmi.html](https://docs.kernel.org/admin-guide/laptops/alienware-wmi.html)
  and [docs.kernel.org/wmi/devices/alienware-wmi.html](https://docs.kernel.org/wmi/devices/alienware-wmi.html).
  Its author (`kuu-rt`) participated directly in issue #434, cross-referencing
  T-Troll's SDK v2 notes and `rwdec` output while building it — this driver is in part
  a downstream beneficiary of this project's reverse-engineering work.
  Coverage is allowlist-based per model (module params `force_platform_profile=1`,
  `force_hwmon=1`, `force_gmode` bypass the allowlist for testing).
- **[`tr1xem/alienfx-linux`](https://github.com/tr1xem/alienfx-linux)** is a working
  CMake + hidapi/libusb port of `AlienFX-SDK` and `AlienFan-SDK` for Linux, built by a
  participant in issue #434, and is the closest thing to an existence proof for this
  roadmap's approach. Its `libusb_helper.cpp` reimplements `HidD_SetFeature`,
  `HidD_SetOutputReport`, `WriteFile`, `ReadFile`, `HidD_GetFeature`,
  `HidD_GetInputReport` as thin wrappers over `hidapi` — i.e. it shims the Windows HID
  calls rather than rewriting the protocol logic, exactly the technique
  [04](04-alienfx-sdk-hid.md) recommends here.
- **[`tr1xem/AWCC`](https://github.com/tr1xem/AWCC)** is a separate, more ambitious
  Linux AWCC alternative (daemon + GUI + CLI) by the same author, layering on top of
  `acpi_call` and the `alienware-wmi` kernel driver, with an explicit low-footprint
  daemon architecture — a useful reference for [09](09-daemon-and-monitor.md).
- Older, narrower prior art also exists and is worth knowing about even though it
  predates and doesn't reuse this codebase's SDKs:
  [`trackmastersteve/alienfx`](https://github.com/trackmastersteve/alienfx) (Python
  CLI/GTK GUI, ships udev rules — useful reference for
  [15](15-packaging-and-permissions.md)) and
  [`rsm-gh/akbl`](https://github.com/rsm-gh/akbl) (older GTK light controller).

None of this eliminates the work described in this roadmap, but it changes the shape of
[05](05-alienfan-sdk-thermal.md): the kernel-driver sysfs path can now be the *primary*
Linux fan backend rather than requiring a from-scratch ACPI implementation for every
supported model.
