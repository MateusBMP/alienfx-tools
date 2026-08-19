# Linux Support Roadmap

This folder is a design/planning package for bringing `alienfx-tools` to Linux. It does
not contain code — it is the analysis and decisions a future implementer needs before
writing any. Every doc references concrete `path:line` locations in this repo and, where
a claim depends on upstream (kernel driver behavior, other projects, maintainer intent),
a link to the source.

## Executive summary

Linux support has never shipped, but not because it's infeasible — see
[`01-why-no-linux-support.md`](01-why-no-linux-support.md). The maintainer said the SDKs
are portable and asked that they stay reusable libraries. The real obstacles are: fan
control depends on Windows-only WMI/a proprietary kernel driver, all configuration is
Windows registry blobs, and the GUI is ~9k lines of raw Win32 dialog code. Since the
maintainer's last "not planned" answer (2024), the Linux kernel gained a native
`alienware-wmi-wmax` driver exposing thermal profiles and fan boost via sysfs, and at
least one working Linux port of the SDKs (`tr1xem/alienfx-linux`) already exists as
proof the approach works.

## Decisions this roadmap assumes

These were made explicitly for this roadmap and shape every doc below:

1. **Cross-platform in-tree.** One repository, one set of SDK sources. Add a CMake
   build and a small platform-abstraction layer; keep the existing `.sln` building for
   Windows. Not a Linux-only fork of the SDKs.
2. **Full scope.** SDKs, CLIs, the daemon/monitor, *and* the GUI are all in scope —
   not just a minimal CLI.
3. **Fan backend: kernel sysfs first, ACPI fallback second.** Prefer the in-tree
   `alienware-wmi` driver's `hwmon`/`platform-profile` sysfs interface; fall back to
   `acpi_call` (porting SDK v1's direct ACPI calls) for models the kernel driver
   doesn't yet recognize.
4. **GUI toolkit: Qt 6.** Closest structural fit to the existing dialog/tab layout,
   native tray support, works on X11 and Wayland.

## Before you start (or resume) research

**[`00-sources.md`](00-sources.md) logs every source this roadmap is built on** —
upstream issues/discussions, kernel docs, prior-art projects, and which local files
were analyzed — with what was extracted from each and which doc(s) it feeds. If you're
updating this roadmap later, start there: it tells you what's already known and
exactly what to re-check for staleness, so an update is an increment on top of existing
research rather than a from-scratch pass. To actually perform that refresh, use the
`update-linux-roadmap` skill (`.claude/skills/update-linux-roadmap/SKILL.md`) — invoke
it with `/update-linux-roadmap` or ask to "update the Linux roadmap." It walks
`00-sources.md`'s checklist, re-checks upstream/kernel/prior-art activity and local
implementation progress, and edits only the specific docs affected.

## Reading order / milestone map

```
01 (why)  →  02 (build)  →  03 (compat layer)
                                   │
                 ┌─────────────────┼─────────────────┐
                 ▼                                    ▼
        04 (HID light SDK)                   05 (fan/thermal SDK)
                 │                                    │
                 ▼                                    ▼
        07 (alienfx-cli)                     08 (alienfan-cli)
                 │                                    │
                 └────────────────┬───────────────────┘
                                   ▼
                          06 (config storage — needed by both CLIs)
                                   │
                                   ▼
                          09 (daemon + monitor)
                                   │
                                   ▼
                          10 (Qt 6 GUI)
                             │   │   │
                 ┌───────────┘   │   └───────────┐
                 ▼                ▼               ▼
        11 (ambient capture) 12 (audio/haptics) 13 (input/hotkeys)
                                   │
                                   ▼
                          14 (LightFX library, lowest priority)

Cross-cutting, apply throughout: 15 (packaging/permissions), 16 (testing)
Full milestone breakdown with entry/exit criteria: 17 (milestones)
```

See [`17-milestones.md`](17-milestones.md) for the actual milestone table (M0–M10) with
sizes and risk notes — the diagram above is the dependency shape, not the schedule.

## Index

| Doc | Topic |
|---|---|
| [00-sources.md](00-sources.md) | Research provenance — every source consulted, what was extracted, how to extend it |
| [01-why-no-linux-support.md](01-why-no-linux-support.md) | Upstream history, real blockers, what's changed since 2024 |
| [02-build-system.md](02-build-system.md) | MSBuild → CMake, keeping both alive |
| [03-platform-abstraction.md](03-platform-abstraction.md) | The Win32→POSIX compat shim |
| [04-alienfx-sdk-hid.md](04-alienfx-sdk-hid.md) | Full light protocol reference + HID port plan |
| [05-alienfan-sdk-thermal.md](05-alienfan-sdk-thermal.md) | Fan/thermal protocol reference + Linux backends |
| [06-configuration-storage.md](06-configuration-storage.md) | Registry schemas → file-based config |
| [07-alienfx-cli.md](07-alienfx-cli.md) | First shippable Linux binary |
| [08-alienfan-cli.md](08-alienfan-cli.md) | Fan CLI port |
| [09-daemon-and-monitor.md](09-daemon-and-monitor.md) | Background monitoring, profile switching |
| [10-gui-qt6.md](10-gui-qt6.md) | GUI port plan |
| [11-ambient-capture.md](11-ambient-capture.md) | Screen-capture ambient lighting |
| [12-audio-haptics.md](12-audio-haptics.md) | Audio-reactive haptics |
| [13-input-and-hotkeys.md](13-input-and-hotkeys.md) | Global hotkeys, key-press lighting |
| [14-lightfx-library.md](14-lightfx-library.md) | `LightFX.dll` → `.so` emulator |
| [15-packaging-and-permissions.md](15-packaging-and-permissions.md) | udev, kernel requirements, distro packaging |
| [16-testing-and-validation.md](16-testing-and-validation.md) | Test strategy given zero existing tests |
| [17-milestones.md](17-milestones.md) | The long roadmap: M0–M10 with entry/exit criteria |

## What we are explicitly not porting (yet)

- **API v0 ACPI light path as a standalone feature.** `tr1xem/alienfx-linux` marks it
  "Not Planned" and it only exists tangled with fan SDK v1; treat it as a stretch goal,
  not a milestone (see [04](04-alienfx-sdk-hid.md) and [05](05-alienfan-sdk-thermal.md)).
- **`alienfan-low`'s generic port/PCI/MSR/physical-memory IOCTLs.** These are a ring-0
  hardware-access backdoor unrelated to Alienware control; nothing in this project needs
  a Linux equivalent of them (see [05](05-alienfan-sdk-thermal.md)).
- **DPTF driver-data files** (`DPTF/*.dvx`) — Intel Dynamic Platform and Thermal
  Framework artifacts consumed by Windows' driver stack; out of scope for a userspace
  Linux port.
