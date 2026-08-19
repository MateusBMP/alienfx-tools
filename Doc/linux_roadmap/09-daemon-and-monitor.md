# Daemon and Monitor: `alienfx-mon` + `alienfan-mon` + Profile Auto-Switch

## Why this needs a new architecture, not a port

`alienfx-mon` (664 lines) and `alienfan-tools/alienfan-mon/MonHelper.cpp` (248 lines)
are Windows tray-resident processes built around APIs with no Linux equivalent at all
(PDH performance counters, `SetWinEventHook`). Rather than trying to find a 1:1
replacement for each Windows call, this doc proposes collapsing "background
monitoring" and "profile auto-switching" into a single Linux daemon
(`alienfxd`, introduced in [02](02-build-system.md)'s target list) that the GUI
([10](10-gui-qt6.md)) talks to as a thin client — this is also the architecture
`tr1xem/AWCC` uses (daemon ~4MB headless, GUI attaches on top), and is a better fit for
Linux's session/systemd model than a second Win32-style tray-icon-per-tool design.

## Sensor monitoring: PDH → `/proc`, hwmon, sensors

`SenMonHelper.cpp:31-153` builds PDH counter queries
(`PdhOpenQuery`/`AddCounter`/`CollectQueryData`/`GetFormattedCounterValue`/
`GetFormattedCounterArray`) against Windows performance counter paths, plus
`GlobalMemoryStatusEx` (`:149`) for memory. A large PDH-based implementation is even
commented out at `alienfx-gui/SysMonHelper.cpp:67-162` (superseded there by WMI calls
to `Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine`,
`SysMonHelper.cpp:62`). None of this maps 1:1; replace by source, not by counter name:

| Windows source | Linux replacement |
|---|---|
| CPU usage counters | `/proc/stat` deltas |
| Memory (`GlobalMemoryStatusEx`) | `/proc/meminfo` |
| Fan RPM / temperature | hwmon (already the fan backend from [05](05-alienfan-sdk-thermal.md) — reuse the same sysfs reads, don't add a second sensor path) |
| Generic hardware temps/voltages | `lm-sensors` (`libsensors`) for sensors outside the `alienware_wmi` hwmon device |
| GPU (`Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine`) | vendor-specific: NVML for NVIDIA, `/sys/class/drm/card*/device/hwmon*` for AMD/Intel — document as best-effort, GPU monitoring parity is not a blocking requirement for this milestone |
| `SYSTEM_POWER_STATUS` (`EventHandler.cpp:96`) | `/sys/class/power_supply/*/` (`capacity`, `status`) |

## Foreground-app-based profile switching: the one feature that doesn't survive

`EventHandler.cpp:236-284,262,267` uses `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` +
`EnumProcesses`/`GetWindowThreadProcessId`/`GetModuleFileNameEx` to detect which
application is focused, driving the GUI's per-app lighting profile trigger
(`Profile-app-<id>-<n>` in `ConfigHandler.cpp`, see [06](06-configuration-storage.md)).

**There is no portable Linux equivalent** — X11 has `_NET_ACTIVE_WINDOW` (works, but
only under X11/XWayland), while Wayland compositors deliberately don't expose global
foreground-window information to arbitrary clients for security reasons; each
compositor that offers anything here does so through its own non-standard extension.
Document this plainly rather than half-implementing it:

- Under X11 (including XWayland-rooted sessions where the compositor allows it):
  implement via `_NET_ACTIVE_WINDOW` + `_NET_WM_PID`/`/proc/<pid>/exe`.
- Under native Wayland: no general solution. Note which compositors expose *something*
  (e.g. KDE/GNOME each have their own extension APIs, forcing per-compositor code) at
  implementation time, but plan for this feature to be **X11-only for the foreseeable
  future** and degrade gracefully (feature simply unavailable, not a crash or a
  misleading "always active" state) elsewhere.
- Other profile triggers that don't depend on foreground-window detection (power
  source, time-based, manual) are unaffected and should work identically everywhere.

## Process model: systemd user service + D-Bus

- **`alienfxd`** runs as a `systemd --user` unit, holding open the HID device handles
  and the fan backend, exposing a small D-Bus interface (session bus) for: get/set
  light state, get/set fan profile/boost, list profiles, subscribe to sensor updates.
  This mirrors `tr1xem/AWCC`'s daemon-first design and is what makes "GUI closed but
  lights/profile still active" work correctly, matching the Windows tools' tray-app
  behavior.
- **Privileged operations** (fan boost writes without a relaxed udev permission,
  `acpi_call` backend access) should go through a narrow polkit action or a small
  setuid/setcap helper invoked by the daemon — not by running the whole daemon as root.
  Decide the exact mechanism in [15](15-packaging-and-permissions.md); this doc's
  requirement is only that the daemon's D-Bus surface doesn't itself need to run
  privileged.
- **Autostart**: a systemd user unit (`~/.config/systemd/user/alienfxd.service`,
  enabled via `systemctl --user enable`) replaces the Windows `AutoStart` registry flag
  and the `schtasks.exe`-based autostart in `Common/Common.cpp:185-199`.

## `RegisterWindowMessage("TaskbarCreated")` and tray-icon lifecycle

Both `alienfx-mon.cpp:25` and `alienfx-gui.cpp` use this Windows idiom to detect
Explorer restarts and re-add the tray icon. `QSystemTrayIcon`
(see [10](10-gui-qt6.md)) handles this class of problem internally on Linux desktop
environments that support the tray protocol at all — no manual re-registration logic
is needed, but note (as [10](10-gui-qt6.md) does) that tray icon support itself is
inconsistent across Wayland compositors.

## Scope boundary with `05`

This doc owns *presentation* of sensor data and *triggering* of profile switches;
[05](05-alienfan-sdk-thermal.md) owns the actual fan/thermal read/write mechanics. The
daemon calls into the same `AlienFan_SDK::Control` Linux backend the CLI uses — don't
create a second code path for "the daemon's view of fan state" vs. "the CLI's view of
fan state".

## Acceptance bar for this milestone

`alienfxd` runs as a user service, exposes current sensor readings and fan/light state
over D-Bus, persists and auto-applies the active profile across restarts, and switches
profile automatically on AC/battery transitions (a trigger with no compositor
dependency) — foreground-app triggers are explicitly best-effort/X11-only per above.
