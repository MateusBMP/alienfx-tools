# AlienFan-SDK: Fan/Thermal Protocol Reference and Linux Backend Plan

## Neither existing SDK ports to Linux

`alienfan-tools/alienfan-SDK/` (v1) and `alienfan-tools/alienfan-SDK_v2/` (v2) are two
mutually exclusive implementations of the same `AlienFan_SDK::Control` API, selected at
build time by the `FANV1` preprocessor define (consumers:
`alienfx-gui/SysMonHelper.cpp:6`, `alienfx-mon/SenMonHelper.cpp:208`,
`alienfan-tools/alienfan-mon/MonHelper.cpp:162`).

- **v1** evaluates ACPI methods through a proprietary, prebuilt, signed **Windows
  kernel driver** (`HwAcc.sys`, loaded via `alienfan-low/`, no driver source in this
  repo). That driver's IOCTL surface (`alienfan-tools/alienfan-SDK/alienfan-low/Ioctl.h:249-294`)
  is a generic ring-0 port I/O / PCI config / MSR / physical-memory backdoor —
  `IOCTL_GPD_READ_PORT_*`, `WRITE_PORT_*`, `READ_PCI`/`WRITE_PCI`, `READ_MSR`/`WRITE_MSR`,
  `MAP_PHYSICAL`/`UNMAP_PHYSICAL`, plus `IOCTL_GPD_OPEN_ACPI` and
  `IOCTL_GPD_EVAL_ACPI_WITH_DIRECT`/`WITHOUT_DIRECT` for the ACPI calls this project
  actually uses. **None of this driver has a Linux equivalent, and none of it should —
  Linux has its own, narrower ACPI method-call mechanism (`acpi_call`, see below).**
- **v2** talks to Windows WMI/COM (`IWbemServices::ExecMethod` against class
  `AWCCWmiMethodFunction`) — WMI is a Windows-only management stack.

**The port is a clean-room reimplementation of the `AlienFan_SDK::Control` public
API against Linux-native interfaces, not a translation of either existing SDK's
transport code.** Keep the public API (`alienfan-tools/alienfan-SDK/alienfan-SDK.h:68-203`, roughly stable across
both variants) as the compatibility boundary so callers (`alienfx-cli`, `alienfan-cli`,
the daemon, the GUI) don't need to know which backend is active.

## Public API to preserve

From `alienfan-tools/alienfan-SDK/alienfan-SDK.h:94-161` (v1) and
`alienfan-tools/alienfan-SDK_v2/alienfan-SDK.h:67-127` (v2), the operations both expose:

`Probe(diskSensors)`, `GetFanRPM`, `GetMaxRPM`, `GetFanPercent`, `GetFanBoost`,
`SetFanBoost(fan, 0..100)`, `GetTempValue`, `Unlock()` (== `SetPower(0)`),
`SetPower`/`GetPower`, `SetGMode`/`GetGMode`. v1-only: `SetGPU(0..4)`,
`GetCharge`/`SetCharge`, `RunMainCommand`/`RunGPUCommand` (raw escape hatch). v2-only:
`GetTCC`/`SetTCC`, `GetXMP`/`SetXMP`, `CallWMIMethod` (raw escape hatch). Shared sensor
type codes: `0=ESIF/TZ, 1=AWCC, 2=Disk, 3=AMD/ECDV, 4=OHM` (`alienfan-tools/alienfan-SDK/alienfan-SDK.h:20`).

The Linux backend should implement this same surface, with `RunMainCommand`/
`CallWMIMethod`'s "raw escape hatch" role filled by a raw-ACPI-call escape hatch (see
Backend B) rather than a WMI one.

## Backend A (primary): kernel `alienware-wmi` sysfs

Since kernel 6.13, the in-tree `alienware-wmi-wmax` platform driver exposes AWCC's
thermal functionality — the same functionality SDK v2 reaches over WMI — directly
through sysfs. This should be the **default, preferred backend** wherever the running
kernel/model combination supports it, because it needs no out-of-tree module, no root
ACPI-call driver, and is maintained upstream. Reference:
[docs.kernel.org/admin-guide/laptops/alienware-wmi.html](https://docs.kernel.org/admin-guide/laptops/alienware-wmi.html)
and [docs.kernel.org/wmi/devices/alienware-wmi.html](https://docs.kernel.org/wmi/devices/alienware-wmi.html).

### Discovery

```
grep -l alienware-wmi /sys/class/platform-profile/platform-profile-*/name
grep -l alienware_wmi  /sys/class/hwmon/hwmon*/name
```

(Note the hyphen/underscore difference: `alienware-wmi` for platform-profile,
`alienware_wmi` for hwmon — both names are correct as written, don't "fix" one to
match the other.) `AlienFan_SDK::Control::Probe()`'s Linux implementation should walk
both trees exactly this way — this is precisely what `tr1xem/alienfx-linux`'s
`AlienFan_SDK::Control::EnumSensorsPath`/`EnumProfilePath`
(`AlienFan-SDK/src/AlienFan-SDK.cpp`) already does, and is a good reference
implementation to consult (not copy verbatim — license/attribution check needed before
reusing code, see [16](16-testing-and-validation.md)).

### Thermal profiles

`/sys/class/platform-profile/platform-profile-N/`:
- `name` → `alienware-wmi` (the match key above)
- `choices` → space-separated list of supported profile names for this model
- `profile` → read/write current profile

Known profile name strings (from kernel docs + `tr1xem/alienfx-linux`'s
`ALIENFAN_PROFILE_NAME`): `custom`, `balanced`, `quiet`, `balanced-performance`,
`performance`, `cool`, `low-power`. `performance` in `choices` implies G-Mode support.
Legacy profile *codes* used internally by AWCC (useful when cross-referencing v1/v2
mappings, not user-facing on Linux): Quiet `0x96`, Balanced `0x97`, Balanced
Performance `0x98`, Performance `0x99`; USTT profiles `0xA0`–`0xA5`; G-Mode `0xAB`
(replaces Performance on G-Series); Custom `0x00` on all models.

### Fan/sensor monitoring and control

`/sys/class/hwmon/hwmonN/` (name `alienware_wmi`) — standard hwmon ABI:
- `fan[1-4]_input` — RPM
- `fan[1-4]_boost` — **read/write, 0–255**. This is a boost value, not a direct PWM
  duty cycle: `pwm = pwm_base + (fan_boost / 255) * (pwm_max - pwm_base)` (empirically
  reverse-engineered relationship, documented in the kernel driver's own docs).
- `temp*_input` — millidegree Celsius, standard hwmon convention.

**Caveat to surface prominently in any UI/CLI**: manual `fan*_boost` writes are
documented to "only work reliably if the `custom` platform profile is selected" on some
devices — the backend should switch to `custom` profile before writing boost, matching
what AWCC itself does, and this maps naturally onto this project's existing
`Unlock()`/`SetPower(0)` semantics from the Windows SDKs.

### Model coverage / module parameters

Coverage is an **allowlist per model** inside the kernel driver (no general
`_HID`-based auto-detection for the newer thermal interface, per the kernel author's
own explanation in issue #434: *"there is no easy way to call WMI... without custom
drivers... which means I have to manually specify each model's quirks"*). Module
parameters exist to bypass the allowlist for testing: `force_platform_profile=1`,
`force_hwmon=1`, `force_gmode`. Document these clearly for users on unsupported models,
and treat "kernel driver present but reports no `alienware_wmi` hwmon/profile" as the
trigger to fall back to Backend B, not as a hard failure.

## Backend B (fallback): `acpi_call` + direct ACPI evaluation

For models the kernel driver's allowlist doesn't (yet) cover, port SDK v1's direct
ACPI approach using the userspace `acpi_call` kernel module
(`/proc/acpi/call`) or, if a maintained wrapper library exists at implementation time,
that instead. This is more model-fragile (method paths vary) but strictly matches what
SDK v1 already does on Windows — nothing conceptually new, just a different ACPI call
transport.

### Per-model ACPI device/method paths

`alienfan-tools/alienfan-SDK/alienfan-controls.h:51-94`:

| # | System | Main method | GPU method | `delta` |
|---|---|---|---|---|
| 0 | Alienware m15/m17 | `\_SB.AMW1.WMAX` | `\_SB.PCI0.PEG0.PEGP.GPS` | 0 |
| 1 | Dell G15 | `\_SB.AMW3.WMAX` | `\_SB.PCI0.GPP0.PEGP.GPS` | 0 |
| 2 | Dell G5 SE / m15R6 | `\_SB.AMWW.WMAX` | `\_SB.PC00.PEG1.PEGP.GPS` | 0 |
| 3 | Aurora R7 (desktop) | `\_SB.AMW1.WMAX` | `\_SB.PCI0.PEG0.PEGP.GPS` | 4 |
| 4 | Area 51 R4 (desktop) | `\_SB.WMI2.WMAX` | `\_SB.PCI0.PEG0.PEGP.GPS` | 4 |

`delta` shifts command IDs by 4 on desktop systems (see below) — this matches
`tr1xem`'s issue #434 comment that Alienware uses prefixes `AMW3`/`AMWW`/`AMW1` (with
`AMW1` also covering Aurora desktops) and that the mapping is genuinely
model-dependent, so **do not hardcode this table as exhaustive** — treat it as a
starting seed and expect new prefixes as new hardware appears. T-Troll's
[`rwdec`](https://github.com/T-Troll/rwdec) tool decodes a `RWEverything` ACPI/BIOS
dump to find which ACPI device (`AW**`/`AMW*`) a given system maps
`AWCCWmiMethodFunction` to — this is the recommended way to add a new model, exactly as
the maintainer described in issue #434: *"You don't need WMI, in fact. You just need to
find all ACPI devices with `_HID = PNP0C14` and then decode `WQMO` buffer to configure
out is WMAX on this device mapped to AWCCWmiMethodFunction or not."*

### Command/sub-command table

`alienfan-tools/alienfan-SDK/alienfan-controls.h:6-22`:

| Operation | `com` | `sub` |
|---|---|---|
| probe / getPowerID (enumerate fans/sensors/powers) | 0x14 | 2 / 3 |
| getFanRPM | 0x14 | 5 |
| getFanPercent | 0x14 | 6 |
| getFanBoost | 0x14 | 0xc |
| setFanBoost | 0x15 | 2 |
| getTemp | 0x14 | 4 |
| getPower | 0x14 | 0xb |
| setPower | 0x15 | 1 |
| setGPUPower | 0x13 | 4 |
| getGMode | 0x25 | 2 |
| setGMode | 0x25 | 1 |
| getSystemID | 0x1a | 2 |
| getFanType (fan→sensor map) | 0x13 | 2 |
| getMaxRPM | 0x14 | 9 |

Call encoding (`alienfan-tools/alienfan-SDK/alienfan-SDK.cpp:150-166`):

```cpp
BYTE operand[4]{ com.sub, value1, value2, 0 };
com.com -= devs[aDev].delta;              // e.g. 0x14 -> 0x10, 0x15 -> 0x11 on desktops
// ACPI call: WMAX(Arg0=0, Arg1=com.com, Arg2=4-byte buffer `operand`)
```

**This 4-byte-buffer encoding is the entire wire format** — reproduce it exactly for
`acpi_call`: evaluate `\_SB.<prefix>.WMAX` with args `(0, command_id, [sub, arg1, arg2, 0])`
and the result comes back as a single integer in the first return argument.

GPU control (`alienfan-tools/alienfan-SDK/alienfan-SDK.cpp:168-184,430-435`): `GPS(0, 0x100, com, buffer4)`;
`SetGPU(power)` → `RunGPUCommand(0x13, power<<4 | 4)`, levels 0–4.

### Additional sensor/EC paths (v1-only, still useful for Backend B)

```cpp
static char pathSEN[]  = "\\_SB.PCI0.LPCB.EC0.SEN",   // thermal-zone sensors
            pathECDV[] = "\\_SB.PCI0.LPCB.ECDV.KDRT",  // AMD/ECDV sensors
            pathCHRG[] = "\\_SB.PCI0.LPCB.EC0.EB0S";   // charge control
```

- Thermal-zone sensors: `\_SB.PCI0.LPCB.EC0.SEN<i>._STR` (name) / `..._TMP` (value),
  `i` 0–9. Value is **deci-Kelvin**: `celsius = (raw - 0xaac) / 0xa` — this conversion
  constant must be reproduced exactly (`alienfan-tools/alienfan-SDK/alienfan-SDK.cpp:385`).
- ECDV sensors: note the path is patched at index 7 between `I`/`0` — i.e.
  `\_SB.PCI0.LPCB.ECDV.KDRT` on Intel systems vs. `\_SB.PC00.LPCB.ECDV.KDRT` on some
  others (`:287,:393`) — called with an integer sensor index; values `< 110` accepted
  as valid.
- Charge control: `\_SB.PCI0.LPCB.EC0.EB0S` — no-arg call reads, one-int-arg call
  writes (`:468-487`).
- G-mode: probes `\_SB.PCI0.LPC0.EC0._Q14` first; if that EC query method exists it's
  read-only status, otherwise does `SetPower(0xAB)` + `setGMode` (`:437-447`).

### Probe algorithm

Iterate `devs[]` calling `probe` (0x14/2); then `getPowerID(index)` walks a single
index space where replies `< 0x100` are fan IDs, `0x100..0x1A0` are sensor IDs, above
that are power-mode IDs (`alienfan-tools/alienfan-SDK/alienfan-SDK.cpp:212-250`). Fan→sensor association via
`getFanType` (`:310-317`).

## A bug in SDK v2 to not inherit

`alienfan-tools/alienfan-SDK_v2/alienfan-controls.h:17-22` has a `functionID` table
indexed by `[sysType][operation]` where `sysType 0` (modern systems) was correctly
re-indexed after `SystemInformation` was removed from `commandList`, but `sysType 1`
(R7-style desktops using the `*2` methods) was **not** — its first 13 entries are
off-by-one, resolving `getThermalInfo2` to `SetThermalControl2`,
`SetThermalControl2` to `TccControl`, and `getFanSensor` to `GetThermalInfo2`. Since
Backend B works from the v1 numeric command IDs (0x10/0x11 for `sysType 1`
Aurora-R7/Area-51 desktops, 0x14/0x15 otherwise — i.e. the `delta`-adjusted values
above) rather than this WMI method-name table, the bug doesn't carry forward
automatically — just don't consult `alienfan-SDK_v2` for desktop command IDs when
implementing Backend B; use the v1 table instead.

## Backend selection and probing order

Recommended `Probe()` sequence for the Linux `AlienFan_SDK::Control`:

1. Look for `alienware-wmi`/`alienware_wmi` sysfs nodes (Backend A). If found, use it
   exclusively for that session — don't mix backends for a single device.
2. If absent, check whether `acpi_call` (or equivalent) is loaded and whether a known
   `\_SB.<prefix>.WMAX` device exists for one of the prefixes in the table above
   (Backend B). Surface a clear message if `acpi_call` isn't loaded, since it's an
   out-of-tree module the user must install separately — this is exactly the class of
   setup friction that produced upstream issue
   [#524](https://github.com/T-Troll/alienfx-tools/issues/524) ("Initial setup failed
   because path for udev is not really a path") on a *different* Linux tool; don't
   repeat that failure mode here — see [15](15-packaging-and-permissions.md).
3. If neither is available, report "fan control unsupported on this system" rather
   than silently no-op'ing — both existing Windows SDKs treat probe failure as fatal
   for fan features while still allowing light control to work standalone; preserve
   that separation of concerns in the daemon/CLI ([08](08-alienfan-cli.md),
   [09](09-daemon-and-monitor.md)).

## Power-profile control (both existing SDKs' consumers)

Windows code additionally calls `PowrProf.lib`
(`PowerGetActiveScheme`/`PowerWriteACValueIndex`/`PowerSetActiveScheme`) at
`alienfx-gui/FanDialog.cpp:41,90,93` and `alienfan-tools/alienfan-cli/alienfan-cli.cpp:235-238`
and `alienfan-tools/alienfan-gui/alienfan-gui.cpp:194,246,249` to flip Windows power
plans alongside thermal profile changes. On Linux there's no equivalent "power plan"
concept in the same sense — the closest mapping is `cpufreq` governor selection plus
the same `platform_profile` sysfs node already used for thermal profile (many desktop
environments already tie their power-mode slider to `platform_profile`). Don't build a
separate power-plan abstraction; document that thermal-profile switching *is* the
Linux power-mode lever, and let `cpufreq` governor changes be an optional additional
knob exposed later if users ask for it.

## Explicitly out of scope

`alienfan-low`'s port I/O / PCI / MSR / physical-memory IOCTLs
(`Ioctl.h:249-294`, listed above) are not used for anything this project's feature set
needs — they exist in the Windows driver as a general hardware-access facility, not
because AWCC-equivalent functionality requires ring-0 memory access. No Linux
equivalent should be built for them.
