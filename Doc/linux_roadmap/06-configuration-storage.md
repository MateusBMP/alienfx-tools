# Configuration Storage: Registry → Files

## Current state: four independent `HKEY_CURRENT_USER` roots

| Root key | Owner | Loader/saver |
|---|---|---|
| `SOFTWARE\Alienfx_SDK` | light/device/group/grid mappings | `AlienFX_SDK.cpp:1045-1156` (`Mappings::LoadMappings`/`SaveMappings`) |
| `SOFTWARE\Alienfxgui` (+ `Profiles`, `Zones` subkeys) | GUI settings, profiles, zone effects | `alienfx-gui/ConfigHandler.cpp:163-339` (load), `:353-609` (save) |
| `SOFTWARE\Alienfan` (+ `Sensors`, `Powers` subkeys) | fan curves, boosts, power-mode names | `alienfan-tools/alienfan-shared/ConfigFan.cpp:40-138` |
| `SOFTWARE\Alienfxmon` (+ `Sensors` subkey) | sensor monitor settings | `alienfx-mon/ConfigMon.cpp:36-97` |

Plus one read-only cross-cutting value: `Software\Microsoft\Windows\DWM\AccentColor`
(`ConfigHandler.cpp:25,147-151`) — Windows accent-color theming, has no Linux
equivalent and should simply not be read on Linux (fall back to a fixed default
accent, handled in [10](10-gui-qt6.md)).

`RegHelperLib` (`RegHelperLib.cpp`, 24 lines) is the shared plumbing under all four:
`GetRegData` (enumerate value #n, allocating the return buffer) and `GetRegString`
(byte buffer → `std::string`, stripping a trailing NUL). Both are Windows Registry API
wrappers with no meaningful logic beyond that — they disappear entirely in the Linux
config backend rather than being ported.

## The hard constraint: binary values are raw MSVC struct dumps

Several `REG_BINARY` values are `memcpy`'d directly from in-memory C structs — e.g.
`ConfigHandler.cpp:431,435,439,456,465,474` write `CustomColors`, per-profile fan
curves, and zone effect data this way. **This is not a portable format** — it encodes
MSVC struct packing/alignment/endianness assumptions. Do not attempt to read these
blobs on Linux or replicate the layout; define a clean, versioned serialization format
instead (this doc recommends JSON; TOML is an acceptable alternative — pick one and
use it consistently, don't mix).

## Full schema, root by root

### `Alienfx_SDK` (light mappings) — `AlienFX_SDK.cpp:1045-1156`

Root values:
- `Dev#<vid>_<pid>` (SZ) — device display name
- `DevWhite#<vid>_<pid>` (DWORD) — white balance
- `DevBright#<vid>_<pid>` (DWORD) — per-device brightness

Subkeys:
- `Light<devID>-<lightID>` → `Name` (SZ), `Flags` (DWORD, packed `flags|scancode<<16`;
  flags `1=ALIENFX_FLAG_POWER`, `2=ALIENFX_FLAG_INDICATOR`, `AlienFX_SDK.h:30-31`)
- `Group<gid>` → `Name` (SZ), `LightList` (BINARY array of `{WORD deviceID; WORD lightID;}`)
- `Grid<id>` → `Name` (SZ), `Size` (DWORD, `x<<8 | y`), `Grid` (BINARY, `x*y` DWORDs)

`devID` is `MAKELPARAM(pid, vid)` (a packed 32-bit device identifier). `SaveMappings`
does a full delete-and-rewrite of the tree (`RegDeleteTree` + rebuild, `:1113`).

### `Alienfxgui` — `ConfigHandler.cpp:163-339` (load) / `:353-609` (save)

Root DWORDs: `AutoStart, Minimized, UpdateCheck, LightsOn, Dimmed, Monitoring,
EffectsOnBattery, GammaCorrection, DisableAWCC, ProfileAutoSwitch, OffWithScreen,
NoDesktopSwitch, DimPower, DimmedOnBattery, TimeoutOn, TimeoutLength, ActiveProfile,
OffPowerButton, DimmingPower (default 164), FullPower (default 255), OffOnBattery,
FanControl, FansOnBattery, ShowGridNames, KeyboardShortcut, GESpeed (default 100),
MonDC, Ambient-Shift, Ambient-Calc, Ambient-Mode, Ambient-Grid, Haptics-Input`. Root
BINARY: `CustomColors` (16×DWORD).

`Profiles` subkey: `Profile-<id>` (name), `Profile-triggers-<id>`,
`Profile-gflags-<id>`, `Profile-app-<id>-<n>`, `Profile-script-<id>`,
`Profile-freq-<id>`, `Profile-fan-<id>-<fan>-<sensor>`,
`Profile-effect-<id>-<groupID>-<mode>`, `Profile-power-<id>`, `Profile-OC-<id>`.

`Zones` subkey: `Zone-flags-<prof>-<group>`, `Zone-eventlist-<prof>-<group>`,
`Zone-colors-<prof>-<group>-<count>`, `Zone-ambient-<prof>-<group>-<size>`,
`Zone-haptics-<prof>-<group>-<count>`, `Zone-effect-<prof>-<group>`.

### `Alienfan` — `ConfigFan.cpp:40-138`

Root DWORDs: `StartAtBoot, StartMinimized, UpdateCheck, LastPowerStage, OC (default
100), DisableAWCC, KeyboardShortcut, KeepSystemMode, PollingRate (default 750),
OCEnable, DiskSensors, NumLockActive`. Root BINARY: `Boost-<fanID>` =
`{byte maxBoost; unsigned short maxRPM;}`.

`Sensors` subkey: `Fan-<fanID>-<sensorID>` (BINARY array of curve points, each a
16-bit word), `SensorName-<type>-<index>` (SZ).

`Powers` subkey: `Power-<index>` (SZ) — user-facing names for raw power-mode codes.

### `Alienfxmon` — `ConfigMon.cpp:36-97`

Root DWORDs: `AutoStart, Minimized, UpdateCheck, wSensors, eSensors, bSensors, Refresh
(default 500)`.

`Sensors` subkey, naming scheme `<field>-<sensorID>`: `0-` name (SZ), `1-` flags
(DWORD), `2-` tray color (DWORD), `3-` alarm point (DWORD).

## Proposed Linux layout

Follow XDG Base Directory conventions:

```
$XDG_CONFIG_HOME/alienfx-tools/           (defaults to ~/.config/alienfx-tools/)
├── lights.json          # replaces Alienfx_SDK: devices, lights, groups, grids
├── gui.json             # replaces Alienfxgui root settings
├── profiles.json        # replaces Alienfxgui\Profiles
├── zones.json           # replaces Alienfxgui\Zones
├── fan.json             # replaces Alienfan (root + Sensors + Powers)
└── monitor.json         # replaces Alienfxmon
```

One file per current registry root (plus splitting `Profiles`/`Zones` out of
`Alienfxgui` for readability) keeps the migration mapping obvious and lets each
component (SDK, GUI, fan daemon) own its own file without lock contention. Use a
top-level `"schemaVersion"` field in every file from day one — the binary blobs this
replaces had no such thing, which is part of why they're unmigratable; don't repeat
that mistake.

A single small config-abstraction library (replacing `RegHelperLib`, living wherever
[02](02-build-system.md)'s `common` target lands) should own read/write/atomic-replace
for these files — atomic replace (write to a temp file, `rename()`) matters here
because `SaveMappings`'s "delete tree then rewrite" pattern (`AlienFX_SDK.cpp:1113`) is
exactly the kind of operation that corrupts a JSON file if interrupted mid-write.

## Non-registry config the port must also carry forward

`alienfx-gui/DevicesDialog.cpp:705` loads `.\Mappings\devices.csv` — the only
non-registry data file in the Windows build (see [04](04-alienfx-sdk-hid.md) for its
schema). On Linux this becomes a read-only data file shipped alongside the binaries
(e.g. `/usr/share/alienfx-tools/devices.csv` or embedded as a resource at build time —
decide based on how [15](15-packaging-and-permissions.md) resolves data-file
installation paths), not a registry migration concern, since it's already file-based on
Windows. Keep the CSV format itself unchanged so device data stays a single
source shared between platforms — don't fork it into JSON just for consistency with the
rest of this doc.

## Windows→Linux migration path

Not a blocking requirement for early milestones, but worth designing for once the
Linux config format is stable: a one-shot import tool that reads a `.reg` export of the
four roots (produced on the Windows side by `alienfx-config.cmd`, which already exists
for backup/restore — see repo root) and writes the equivalent JSON files. This is
strictly an ergonomics feature for users dual-booting or migrating machines; sequence
it after [07](07-alienfx-cli.md)/[08](08-alienfan-cli.md) ship, not before.
