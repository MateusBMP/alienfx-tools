# AlienFX-SDK: Light Protocol Reference and HID Port Plan

This is the single most important doc in this roadmap — it's the full protocol
reference an implementer needs so they never have to reverse-engineer
`AlienFX_SDK.cpp`/`alienfx-controls.h` from scratch. Every packet layout below was
extracted directly from those two files.

## `Functions` vs. `Mappings`: what M2 actually ports

`AlienFX_SDK.cpp` (1177 lines) is two unrelated classes, not one. This determines M2's
scope directly:

| Region | Class | What it does | Windows-only calls | Owner |
|---|---|---|---|---|
| `:24-890`, `:1174-1177` | `Functions` | The HID protocol state machine below — device init, packet construction, transport | 6 HID functions + `Sleep` in `PrepareAndSend`/`GetDeviceStatus` (`:95-146`, `:787-820`); SetupAPI only in `AlienFXProbeDevice`/`AlienFXInitialize` (`:178-275`) | **M2** |
| `:893-1172` | `Mappings` | Registry-backed light-name and grid persistence (`Load/SaveMappings`, device list bookkeeping) | 37 `Reg*`/`HKEY`/`SetupDi`/`CloseHandle` references | [M4](06-configuration-storage.md) |

`Functions` has zero references into `Mappings`, `HKEY`, or `Reg*` — the split is a plain
`#ifdef _WIN32` wrap, not a rewrite. One consequence worth calling out because it
corrects an M1 hand-off note: `tests/alienfx_sdk/sdk_headers_test.cpp:133-168` flagged
three brace-elision call sites (`AlienFX_SDK.cpp:918,999,1077`) that don't compile clean
under `-Werror` as something "M2 must fix". All three are inside `Mappings`
(`AlienFxUpdateDevice`, `AddDeviceById`, `LoadMappings`) — they move to M4's scope. This
is also why M2's first sub-milestone can be **purely additive** to the pre-existing file
(no line deleted) despite compiling `AlienFX_SDK.cpp` on Linux for the first time: the
part that needed non-additive rewriting to compile clean isn't part of M2 at all.

## M2's sub-milestones, mapped onto this doc

See [17-milestones.md](17-milestones.md) for the full goal/exit/size/risk breakdown of
each; this is the short version of *where in this doc* each one lives:

| Sub-milestone | What | Where in this doc |
|---|---|---|
| **M2a** | Compile `Functions` behind a recording fake transport; freeze `source-derived` golden vectors for all 7 versions; `hand-derived` tests for the 4 fragile spots | "The transport core", all seven "Per-version command tables" subsections, "Timing is not optional" |
| **M2b** | Real `hidapi` transport backend + `--dry-run` (done) | "hidraw / hidapi mapping", "M2b's decided mapping and the four defects it found" |
| **M2c** | hidraw enumeration + sysfs-based detection; fixes the 33-vs-34 report-length bug (done) | "API version enum and detection", "Linux enumeration path (M2c), for comparison" |
| **M2d** | Live API_V4 validation | N/A — hardware step, see `local/test-machine.md` |
| **M2e** | V5 collection-aware detection design | "API version enum and detection", the V5/Darfon row |

The four spots this doc and [16](16-testing-and-validation.md) call fragile — the V8
feature-vs-interrupt size heuristic, the V6 XOR checksum, the V2 4-bit color packing, and
V7's write-then-read requirement — are each called out in their per-version subsection
below and are exactly what M2a's `hand-derived` tests target.

## API version enum and detection

`AlienFX-SDK/AlienFX_SDK/AlienFX_SDK.h:97-108`:

```cpp
enum Afx_Version {
    API_ACPI = 0, //128           -- see the ACPI note at the end of this doc
//  API_V9 = 9, //193             -- scaffolded, not implemented (new monitors)
    API_V8 = 8, //65
    API_V7 = 7, //65
    API_V6 = 6, //65
    API_V5 = 5, //64
    API_V4 = 4, //34
    API_V3 = 3, //12
    API_V2 = 2, //9
    API_UNKNOWN = -1
};
```

API v1 was removed upstream ("Ancient notebooks — deprecated and removed",
`AlienFX-SDK/README.md:17`). Do not resurrect it.

**Detection is not a static VID/PID table** — it's `VID + HID OutputReportByteLength`,
decided in `AlienFXProbeDevice` (`AlienFX_SDK.cpp:191-236`):

```cpp
length = caps.OutputReportByteLength;
pid = attributes.ProductID;
switch (vid = attributes.VendorID) {
case 0x0d62: // Darfon
    if (caps.Usage == 0xcc && !length) {       // note: OutputReportByteLength == 0
        length = caps.FeatureReportByteLength;  // real size comes from feature report
        version = API_V5;
    }
    break;
case 0x187c: // Alienware
    switch (length) {
    case 9:  version = API_V2; break;
    case 12: version = API_V3; break;
    case 34: version = API_V4; break;
    case 65: version = API_V6; break;
    }
    break;
default:
    if (length == 65)
        switch (vid) {
        case 0x0424: if (pid != 0x274c) version = API_V6; break; // Microchip (0x274c = hub, excluded)
        case 0x0461: version = API_V7; break;                    // Primax (mice)
        case 0x04f2: version = API_V8; break;                    // Chicony (external keyboards)
        }
}
```

| VID | Vendor | Device class | Versions seen |
|---|---|---|---|
| `0x187c` | Alienware | chassis/tron/power lights, monitors | V2, V3, V4, V6 |
| `0x0d62` | Darfon | internal per-key RGB keyboards | V5 (feature reports only, `Usage == 0xcc`) |
| `0x0424` | Microchip | monitors (excl. PID `0x274c`, a hub) | V6 |
| `0x0461` | Primax | mice | V7 |
| `0x04f2` | Chicony | external keyboards (AW410k/510k) | V8 |

**Two confirmed Linux porting defects in this switch, found by probing real hardware**
(`local/test-machine.md`, both are **M2c**'s to fix, not M2a's):

- **Finding 1 (`local/test-machine.md:28-54`) — fixed in M2c.** `caps.OutputReportByteLength`
  on Windows *includes* the leading report-ID byte; a Linux HID report descriptor's Report
  Count does not when the device has no `85 xx` Report ID item. A real 34-byte V4 controller
  (this fork's test machine: `187c:0550`) therefore parses as 33 on Linux, matches no
  `case` above, and is silently left at `API_UNKNOWN` — no error, no lights. **Fix: add 1
  to the parsed Report Count before this switch to normalize to the Windows convention,
  not add a `case 33`** — hidraw `write(2)` still expects the leading report-ID byte on
  the wire, so 34 remains the true on-wire size either way. Implemented in
  `hid_report_descriptor.cpp`'s `ParseHidReportDescriptor`: the `+1` is applied only when
  a report of that kind exists at all (an all-zero collection stays byte length 0, not 1 —
  Finding 2's eventual fix depends on that zero being reachable). Confirmed live against
  `187c:0550`'s real descriptor, both in `tests/alienfx_sdk/report_descriptor_test.cpp`
  and by running `tests/support/probe_demo.cpp` against the actual hidraw node.
- **Finding 2 (`local/test-machine.md:56-87`) — still open, narrowed by M2c.** The Darfon
  `case 0x0d62` condition is evaluated by Windows *per top-level HID collection* (each gets
  its own device interface path there), but Linux hidraw exposes a whole composite
  interface as one node — including sibling collections (e.g. the boot-keyboard LED output
  report) that make `OutputReportByteLength == 0` false even when the vendor-usage V5
  collection is present. A literal port never detects V5. Needs a deliberate redesign
  (M2e), not a mechanical translation of this condition.

  **What M2c found while building the enumeration path that narrows this for M2e**:
  `hid_enumerate()` (hidapi's hidraw backend) already yields one `hid_device_info` entry
  *per top-level collection*, each carrying that collection's own `usage`/`usage_page` —
  confirmed live: this fork's test machine's Darfon node (`0d62:3740`, one hidraw path)
  enumerates as at least two entries, the first with `usage_page=0x0001`/`usage=0x0006`
  (Generic Desktop/Keyboard — the boot-keyboard collection) ahead of the vendor-usage one.
  So the *usage* half of Windows' per-collection view is already available on Linux, for
  free, at enumeration time — M2c's `hid_enumerate_linux.cpp` deliberately dedupes these
  down to one `HidNode` per hidraw path (keeping the first entry's usage) since
  `AlienFXProbeDevice` only ever needs one node per path to decide what to open. What's
  still missing is the *report-length* half: `hid_report_descriptor.cpp`'s parser
  deliberately aggregates Output/Feature bit lengths across the **whole node** (every
  top-level collection in one descriptor blob), not per collection — this is what makes
  it correctly reproduce Finding 2 (the boot-keyboard's Output report keeps the aggregate
  non-zero, so V5 stays undetected, matching Windows' real per-collection behavior for a
  *different* reason than a literal port would). M2e's job is narrower than originally
  scoped: re-associate each top-level collection's *own* report lengths with the
  `usage`/`usage_page` hidapi already hands back per-collection (e.g. bucket
  `hid_report_descriptor.cpp`'s existing per-Report-ID accumulation by collection depth
  instead of merging it into one whole-node max) — not "enumerate sibling hidraw nodes by
  USB interface", since there is only one hidraw node to begin with. Confirmed live: this
  machine's Darfon node reports `out=2 feat=8` (whole-node aggregate) via `probe_demo`,
  matching `feat=8`'s prediction in `local/test-machine.md` ("7 bytes... 8 with its ID")
  exactly.

  **Accepted gap, not exercised by any known device**: this same whole-node aggregation
  means a hypothetical composite `0x187c` device whose sibling top-level collections have
  *different* Output report sizes (e.g. one at 34 bytes, another at 65) could misdetect
  V4 as V6 — `PrepareAndSend`'s V6 arm (`WriteFile`, `0xFF`-padded) is a materially
  different wire protocol from V4's (`HidD_SetOutputReport`). No device on this fork's
  test machine has this shape; a proper fix is the same per-collection TLC-scoped
  accumulation M2e needs for Finding 2, not a separate mechanism.

Enumeration path: `SetupDiGetClassDevs(GUID_DEVINTERFACE_HID, DIGCF_PRESENT |
DIGCF_DEVICEINTERFACE)` → `SetupDiEnumDeviceInterfaces` → `CreateFile(DevicePath,
GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE)` → `HidD_GetAttributes` /
`HidD_GetPreparsedData` / `HidP_GetCaps` (`AlienFX_SDK.cpp:261-274` single device,
`:926-969` `AlienFXEnumDevices` used by the GUI). Manufacturer/product strings via
`HidD_GetManufacturerString`/`HidD_GetProductString` (`:243-252`). Comm timeouts
`{100,0,0,10,200}` set at `:180,:252`.

**Linux enumeration path (M2c), for comparison**: `hid_enumerate_linux.cpp`'s
`EnumerateNodes` calls hidapi's `hid_enumerate(vid, pid)` (no device opened), pre-filters
to the five known AlienFX vendors, dedupes hidapi's one-entry-per-top-level-collection
results down to one `HidNode` per hidraw path, then reads
`/sys/class/hidraw/hidrawN/device/report_descriptor` directly and parses it with
`hid_report_descriptor.cpp` — **not** `hid_open_path()` followed by
`hid_get_report_descriptor()`. This is a deliberate departure from the obvious literal
port, not an oversight: the sysfs attribute is world-readable (confirmed on this fork's
test machine: `-r--r--r--`, versus the hidraw device node's `crw------- root:root`, no
udev rule until M2d), so detection needs no privilege at all. Opening
(`alienfx_hid::OpenNode`, `hid_open_path` + `RegisterDevice`) happens only for a node
whose descriptor already resolved to a known version, and only that one node. Reading
sysfs first also breaks what would otherwise be a circular dependency: a design that opens
first to determine the version could not demonstrate M2c's own exit criterion
(`187c:0550` resolves to `API_V4`) without root, since the udev rule that would make
opening work non-root is M2d's, not M2c's.

## The transport core: `PrepareAndSend`

`AlienFX_SDK.cpp:95-146` is the single choke point every packet goes through:

```cpp
byte buffer[MAX_BUFFERSIZE];                          // MAX_BUFFERSIZE == 193
memset(buffer, version == API_V6 ? 0xff : 0, length); // V6 pads with 0xFF, all others 0x00
memcpy(buffer, command, command[0] + 1);               // command[0] is a LENGTH byte, not data
buffer[0] = reportIDList[version];                     // report ID overwrites that length byte
if (mods) for (auto i : *mods) memcpy(buffer + i.i, i.vval.data(), i.vval.size());

switch (version) {
case API_V2: case API_V3: case API_V4:
    return HidD_SetOutputReport(devHandle, buffer, length);
case API_V5:
    return HidD_SetFeature(devHandle, buffer, length);
case API_V6:
    return WriteFile(devHandle, buffer, length, &written, NULL);
case API_V7:
    WriteFile(devHandle, buffer, length, &written, NULL);
    return ReadFile(devHandle, buffer, length, &written, NULL);   // V7 always reads back
case API_V8:
    if (needV8Feature) { Sleep(4); res = HidD_SetFeature(...); Sleep(6); return res; }
    else return WriteFile(devHandle, buffer, length, &written, NULL);
}
```

Two structural facts to preserve exactly in a reimplementation:

1. **Commands are `{len, b1, b2, ...}` templates**, where byte 0 is a *payload length*
   that gets overwritten with the report ID before sending — it is never part of the
   wire payload itself.
2. **`Afx_icommand {int i; vector<byte> vval;}`** is "patch these bytes at absolute
   buffer offset `i`". All packet construction in this SDK is offset-patching over a
   template buffer, not building packets field-by-field. Preserve this model in the
   Linux port — it's what makes `alienfx-controls.h`'s tables directly reusable.

Report IDs and brightness scale, indexed by `Afx_Version`
(`alienfx-controls.h:13-15`):

```cpp
//                          v0    1    2    3    4    5    6    7   8    9
const byte    reportIDList[]{ 0,   2,   2,   2,   0,0xcc,   0,   0,  1,   0 };
const byte brightnessScale[]{0xf,0x64,0x64,0x64,0x64,0xff,0x64,0x64,0xa,0x64};
```

Transport summary:

| API | Size | Report ID | Write | Read/status |
|---|---|---|---|---|
| V2 | 9 | 2 | `SET_OUTPUT_REPORT` | `GET_INPUT_REPORT`, status in `buffer[0]` |
| V3 | 12 | 2 | `SET_OUTPUT_REPORT` | same as V2 |
| V4 | 34 | 0 | `SET_OUTPUT_REPORT` | `GET_INPUT_REPORT`, status in `buffer[2]` |
| V5 | 64 | 0xcc | `SET_FEATURE` | `GET_FEATURE`, status in `buffer[2]` |
| V6 | 65 | 0 | interrupt write (`WriteFile`) | none |
| V7 | 65 | 0 | interrupt write **then** interrupt read | read result unused by caller |
| V8 | 65 | 1 | `SET_FEATURE` for 1-byte cmds, interrupt write for data blocks | none |

### hidraw / hidapi mapping

The commented-out `DeviceIoControl` variants already in the source spell out the
Linux ioctl equivalents directly — they were apparently a prior exploration of the raw
approach:

| Windows call | Commented alternative (`AlienFX_SDK.cpp`) | Linux hidraw ioctl | hidapi call |
|---|---|---|---|
| `HidD_SetFeature` | `:800` | `HIDIOCSFEATURE` | `hid_send_feature_report` |
| `HidD_SetOutputReport` | `:116` | plain `write()` with report-ID-prefixed buffer | `hid_send_output_report` (or `hid_write` per below) |
| `HidD_GetFeature` | `:806` | `HIDIOCGFEATURE` | `hid_get_feature_report` |
| `HidD_GetInputReport` | `:815` | plain `read()` | `hid_get_input_report` |
| `WriteFile` (interrupt) | `:120` | plain `write()` | `hid_write` |
| `ReadFile` (interrupt) | — | plain `read()` | `hid_read` |

**Recommended technique** (matches `tr1xem/alienfx-linux`, which already validated
this against real hardware): don't rewrite `PrepareAndSend`'s switch statement at all.
Reimplement the six Windows function names it calls
(`HidD_SetFeature`/`HidD_SetOutputReport`/`WriteFile`/`ReadFile`/`HidD_GetFeature`/
`HidD_GetInputReport`) as thin wrappers over `hidapi`, taking `hid_device*` instead of
`HANDLE`:

```cpp
// libusb_helper.h (Linux only)
bool HidD_SetFeature(hid_device* dev, uint8_t* buf, size_t len) {
    return hid_send_feature_report(dev, buf, len) >= 0;
}
bool WriteFile(hid_device* dev, uint8_t* buf, size_t len) {
    return hid_write(dev, buf, len) >= 0;
}
// ...HidD_GetFeature -> hid_get_feature_report, ReadFile -> hid_read, etc.
```

This keeps `alienfx-controls.h` and all of `PrepareAndSend`/`SetMaskAndColor`/etc.
byte-for-byte identical between platforms — only the six function *definitions* and the
`devHandle` type (`HANDLE` → `hid_device*`, `AlienFX_SDK.h:124`) change. Backend choice:
`hidapi` supports both a `hidraw` backend (Linux-native, no libusb dependency, simpler
udev rules) and a `libusb` backend (needed if any device requires control transfers
hidapi's hidraw backend doesn't expose — V7's read-after-write and V8's mixed
feature/interrupt pattern are both within plain hidapi's capability, so hidraw should
suffice; keep libusb as a documented fallback if a specific device model proves
otherwise). Decide the specific backend in implementation, not in this doc — verify
against real V6/V7 hardware since those are the interrupt-transfer paths.

`GetMaxPacketSize`-style endpoint introspection is only needed if a libusb backend is
chosen (`tr1xem/alienfx-linux`'s `libusb_helper.cpp` includes one that walks
`libusb_get_config_descriptor` for the `LIBUSB_TRANSFER_TYPE_INTERRUPT` IN endpoint) —
skip it entirely if hidraw suffices.

### M2b's decided mapping and the four defects it found

**Status: done.** `AlienFX-SDK/AlienFX_SDK/hid_backend_linux.cpp` implements exactly the
table above, over `hidapi-hidraw` (headers only at link time — see `hid_backend_linux.cpp`'s
own file comment for why). Confirmed against this fork's installed hidapi 0.15.0:

| `hid_backend.h` symbol | hidapi call | Return mapping |
|---|---|---|
| `HidD_SetOutputReport` | `hid_send_output_report` (hidapi >= 0.15.0) / `hid_write` (older) | `>= 0` -> `TRUE` |
| `HidD_SetFeature` | `hid_send_feature_report` | `>= 0` -> `TRUE` |
| `HidD_GetFeature` | `hid_get_feature_report` | `>= 0` -> `TRUE` |
| `HidD_GetInputReport` | `hid_get_input_report` | `>= 0` -> `TRUE` |
| `WriteFile` | `hid_write` | `>= 0` -> `TRUE`, `*written = n` |
| `ReadFile` | `hid_read_timeout(..., 100)` | `-1` -> `FALSE`; `0` (timeout) -> `TRUE`, `*read = 0` |
| `CloseHandle` | `hid_close` | always `TRUE` |
| `Sleep` | `std::this_thread::sleep_for` | — |

Four porting defects surfaced by actually writing this file, none visible from reading
`AlienFX_SDK.cpp` alone:

1. **`Get*` calls need `buffer[0]` pre-set to the requested report ID.** hidapi's
   `hid_get_feature_report`/`hid_get_input_report` docs are explicit: "Set the first byte
   of `data[]` to the Report ID of the report to be read... Upon return, the first byte
   will still contain the Report ID, and the report data will start in `data[1]`." The
   underlying `HIDIOCGFEATURE`/`HIDIOCGINPUT` ioctls have the same contract. But
   `GetDeviceStatus` (`AlienFX_SDK.cpp:872-905`) passes a fresh, uninitialized stack
   buffer straight into `HidD_GetFeature`/`HidD_GetInputReport` — V5's report ID is
   `0xcc` (`reportIDList[API_V5]`), so an uninitialized byte 0 there requests a
   nonexistent report and the call legitimately fails. **Fixed**: three additive
   `#ifndef _WIN32` lines set `buffer[0] = reportIDList[version]` immediately before each
   `Get*` call — purely additive, `Mappings`/Windows untouched, same convention M1/M2a
   established. What this fix deliberately does *not* touch: V2/V3's
   `return buffer[0]` (`AlienFX_SDK.cpp:901`) reads the *response* byte, and hidapi's own
   doc comment above implies `HidD_GetInputReport`'s response, like `GetFeature`'s, keeps
   the echoed report ID at byte 0 with real data starting at byte 1 for a *numbered*
   report (V2/V3's report ID is `2`, not `0`) — which would mean `buffer[0]` is the
   report ID, not the status, making that pre-existing read questionable on real
   hardware. Reinterpreting which offset holds the status is a protocol-correctness
   question needing V2/V3 hardware to settle (none exists on this fork's test machine,
   see `local/test-machine.md`), not something to silently reinterpret while fixing an
   unrelated uninitialized-input bug — recorded here, not fixed, same treatment as the
   `SavePowerBlock` defect below.
2. **`ReadFile`'s timeout is not an error.** Windows' `ReadFile` (with the
   `COMMTIMEOUTS` this SDK sets, `AlienFX_SDK.cpp:180,252`) returns `TRUE` with 0 bytes on
   a timeout. `hid_read_timeout` returns `0` on timeout and `-1` on error — collapsing
   both to "failure" would make every V7 operation report failure, since V7's
   read-after-write (`AlienFX_SDK.cpp:192-194`) exists purely to complete the transaction
   and its result is never inspected by the caller. **Fixed**: only `-1` maps to `FALSE`;
   `0` maps to `TRUE` with `*read = 0`. Uses `hid_read_timeout(..., 100)` rather than a
   blocking `hid_read`, mirroring the 100ms `ReadIntervalTimeout` already set, so an
   unresponsive device can't hang the process.
3. **`HidD_SetOutputReport` and `hid_write` are different transfers.** Windows'
   `SET_OUTPUT_REPORT` is a control-endpoint `Set_Report` transfer; `hid_write` is an
   interrupt transfer — genuinely different on the wire, and V2/V3/V4 all go through
   this call. hidapi's matching call, `hid_send_output_report`, only exists from 0.15.0.
   **Fixed**: `#if HID_API_VERSION >= HID_API_MAKE_VERSION(0, 15, 0)` picks
   `hid_send_output_report` when available and falls back to `hid_write` at compile time
   otherwise (Debian/Ubuntu LTS ships older hidapi); `ALIENFX_HID_OUTPUT_MODE=report|write`
   picks between the two at runtime once both exist, so M2d can settle which one real
   V2/V3/V4 hardware actually wants without a rebuild.
4. **hidapi's own source needs `gnu11`, not `c11`.** `linux/hid.c` uses `wcsdup`/
   `strdup`/`strtok_r`/`O_CLOEXEC` (POSIX/GNU libc extensions) with no feature-test macro
   of its own, relying on being compiled as `gnu11`. This project's project-wide
   `CMAKE_C_EXTENSIONS OFF` (deliberate, see [02](02-build-system.md)) is inherited by
   `FetchContent`'s subdirectory build unless overridden, which silently turned that into
   `c11` and hid every one of those declarations — caught by actually exercising the
   `gcc-fetched` preset, not by reading hidapi's source. **Fixed**: `CMAKE_C_EXTENSIONS`
   is toggled `ON` then back `OFF` bracketing just the `hidapi` `alienfx_require_package()`
   call in the top-level `CMakeLists.txt`; only affects the FetchContent (source-build)
   branch — the system-package branch links a prebuilt `.so` and never compiles `hid.c`.

**Where the dependency is declared**: the top-level `CMakeLists.txt`, not
`AlienFX-SDK/AlienFX_SDK/CMakeLists.txt`. `find_package()`'s `IMPORTED` targets
(`hidapi::include`, `hidapi::hidraw`) are only visible in the directory that called
`find_package()` and that directory's own subdirectories — `AlienFX-SDK/AlienFX_SDK`
(which needs `hidapi::include` for `alienfx::hid_linux`) and `tests/` (which needs both
for `fake_hidapi`/`dry_run_demo`) are siblings, so the shallowest common ancestor is the
project root.

**Vendor allowlist and dry run**, the safety posture [17](17-milestones.md)'s M2b section
mentions: `hid_backend_linux.h` exposes `alienfx_hid::SetAllowAnyVendor`/`SetDryRun`/
`SetDryRunSink`/`SetOutputMode`, each seedable once from an environment variable
(`ALIENFX_ALLOW_ANY_VENDOR`/`ALIENFX_DRY_RUN`/`ALIENFX_HID_OUTPUT_MODE`) and overridable
at runtime. `RegisterDevice(HANDLE, vid, pid)` is the registry the allowlist gate checks
first, falling back to `hid_get_device_info()` (no I/O, cached on the handle at open
time) when a handle wasn't registered — `hid_enumerate_linux.cpp`'s `OpenNode` (M2c) is
now the real production caller of `RegisterDevice`, immediately after a successful
`hid_open_path()`; `tests/support/dry_run_demo.cpp` and various tests still call it
directly too, for the same reason M2b needed to (it links the stub enumeration seam, not
the real one — see `tests/CMakeLists.txt`).

**This same seam is what splits M2a from M2b.** `AlienFX-SDK/AlienFX_SDK/hid_backend.h`
declares exactly these six symbols (plus `Sleep`/`CloseHandle`) with signatures matching
the existing call sites, so `PrepareAndSend`/`GetDeviceStatus` need no changes either way.
M2a links `Functions` against `tests/support/fake_hid.cpp` — a backend that logs calls
instead of touching a device, used to freeze golden vectors with no hidapi dependency and
no hardware. M2b adds `hid_backend_linux.cpp`, the real `hidapi` implementation detailed
in the next section; its own tests link that same real file against
`tests/support/fake_hidapi.cpp` (a stub of *hidapi's* API, one layer further down) rather
than a real device, so the regression check that M2b's real backend still produces the
exact same bytes needs no hardware either — see [17](17-milestones.md)'s M2b section for
why that split exists.

### Timing is not optional

`Sleep()` calls in the protocol state machine
(`AlienFX_SDK.cpp:134,136,771,829,832,845,852`) are real device handshake delays (2–20
ms), not incidental Windows scheduling artifacts. Preserve them as `std::this_thread::sleep_for`
verbatim — do not "optimize" them away, and do not assume the delays are safe to shorten
without hardware to test against.

## Per-version command tables

### V2/V3 (9/12 bytes, old notebooks: m14x/17x, 13R1/R2)

`alienfx-controls.h:17-43`:

```cpp
const byte COMMV1_color[]{ 1, 0x03 };      // [1] 1-3 effect type, [2] chain seq,
                                             // [4-6] light mask, [rest] RGB, RGB2
const byte COMMV1_loop[]{ 1, 0x04 };
const byte COMMV1_update[]{ 1, 0x05 };
const byte COMMV1_status[]{ 1, 0x06 };
const byte COMMV1_reset[]{ 2, 0x07, 0x04 }; // [2]: 1 power&indicator, 2 sleep, 3 off, 4 on
const byte COMMV1_saveGroup[]{ 1, 0x08 };
const byte COMMV1_save[]{ 1, 0x09 };
const byte COMMV1_setTempo[]{ 1, 0x0e };
const byte COMMV1_apply[]{ 2, 0x1d, 0x03 };
const byte COMMV1_dim[]{ 3, 0x1c, 0x64, 0x1 };  // [2] brightness, [3] 1=always/0=battery-only
const byte v1OpCodes[]{ 3, 2, 1, 1, 1, 1, 1 };  // indexed by Action enum
```

Packet build (`AlienFX_SDK.cpp:24-89`, `SetMaskAndColor`): light selection is a
**bitmask**, `1 << lightIndex` (or `~(1 << lightIndex)` for inverse selection). For
V2/V3 the mask+opcode+chain go at offset 1 via `{1, {opcode, chain, mask.r, mask.g,
mask.b}}` (colorcode union spreads the 24-bit mask across 3 bytes). Color:

- **V3**: 6 raw bytes at offset 6 — `{c1.r, c1.g, c1.b, c2.r, c2.g, c2.b}` (full 8-bit).
- **V2**: 3 packed bytes at offset 6 — 4 bits per channel:
  ```cpp
  {(c1.r&0xf0)|((c1.g&0xf0)>>4), (c1.b&0xf0)|((c2.r&0xf0)>>4), (c2.g&0xf0)|((c2.b&0xf0)>>4)}
  ```

`chain` is a sequence counter incremented after each `COMMV1_loop` send
(`AlienFX_SDK.cpp:162,168,451,580`). Tempo/duration: `tempo<<3` and `time<<5`,
big-endian 16-bit at offsets 2–5 of `COMMV1_setTempo` (`:564-569`).

Handshake: `Reset()` sends `COMMV1_reset`, polls for status `ALIENFX_V2_READY (0x10)`
(`AlienFX_SDK.h:15-17`, poll loop `WaitForReady` `:822-837`, up to 100×10ms);
`UpdateColors()` sends `COMMV1_update`.

Power-button lighting is a scripted 6-block sequence (`SavePowerBlock`,
`SetPowerAction`): group `2` (AC sleep morph, saved twice with inverted
mask), `5` (AC power color), `6` (charge morph), `7` (battery standby), `8` (battery),
`9` (battery critical pulse).

**Pre-existing defect found by M2a's characterization testing, independent of the
port**: `SavePowerBlock` (`AlienFX_SDK.cpp:212-240`) declares `group = {{2, {blID}}}`
(one element) and passes `&group` to `PrepareAndSend` **five** times (`:214, :219,
:224, :227, :230`) without ever repopulating it. `PrepareAndSend` always clears
`*mods` after use (`:174`'s `mods->clear()`), so every call after the first passes an
*empty* vector — and `PrepareAndSend`'s `needV8Feature = mods->front().vval.size() ==
1` (`:174`) calls `.front()` on `*mods` whenever `mods` is non-null, **for every API
version, not just V8**, with no emptiness check. The unconditional call at `:230` means
this fires on *every* `SavePowerBlock` invocation regardless of the
`needSecondary`/`needInverse` flags — i.e. every V2/V3 `SetPowerAction` and every
V2/V3 `SaveLightsState` call that includes a non-empty light list. This is undefined
behavior (silently reads garbage on MSVC release builds, which is presumably why it
has gone unnoticed) and aborts outright under a hardened libstdc++
(`vector::front(): assertion '!this->empty()' failed`) — which is how M2a's
`tests/support/gen_golden` found it. **Not fixed as part of M2a** (a behavior change
to shared, MSVC-authored code needs deliberate review, not a characterization-milestone
side effect) — tracked by excluding V2/V3 from
`tests/support/packet_matrix.cpp`'s `SetPowerAction`/`SaveLightsState` cases (see that
file's comment) until it is.

### V4 (34 bytes, "tron" — modern notebooks/desktops/Aurora R8+)

`alienfx-controls.h:45-73`:

```cpp
const byte COMMV4_control[]{ 6, 0x03, 0x21, 0x00, 0x03, 0xff, 0xff };
// [4] control type: 1 start-new, 2 finish+save, 3 finish+play, 4 remove,
//                    5 play, 6 set-default, 7 set-startup
// [5-6] control ID: 0xffff common, 8 startup, 61 light
const byte COMMV4_colorSel[]{ 5, 0x03, 0x23, 0x01, 0x00, 0x01 };
// [3] 1=loop 0=once; [5] light count; [6-33] light IDs (indices, NOT a bitmask)
const byte COMMV4_colorSet[]{ 7, 0x03, 0x24, 0x00, 0x07, 0xd0, 0x00, 0xfa };
// [3] action type (0 light,1 pulse,2 morph); [4] phase length;
// [5] mode: d0 light, dc pulse, cf morph, e8 power-morph, 82 spectrum, ac rainbow;
// [7] tempo (0xfa = steady); [8-10] rgb; up to 3 more phases at [11-17],[18-24],[25-31]
const byte COMMV4_setPower[]{ 2, 0x03, 0x22 };
const byte COMMV4_turnOn[]{ 2, 0x03, 0x26 };      // [4] brightness 0..100, [5] count, [6-33] IDs
const byte COMMV4_setOneColor[]{ 2, 0x03, 0x27 }; // [3-5] rgb, [7] count, [8-33] IDs
static byte v4OpCodes[]{ 0xd0, 0xdc, 0xcf, 0xdc, 0x82, 0xac, 0xe8 };
```

- Reset: `COMMV4_control` `[4]=4` (remove) then `[4]=1` (start new), `AlienFX_SDK.cpp:314-319`.
- Update: bare `COMMV4_control` (template default `[4]=3`, finish+play), `:343-346`.
- Bulk same-color: `SetMultiColor` (`:408-428`) — max **26 IDs per packet** (offsets
  8..33), auto-chunks with Update+Reset between batches for more.
- Per-light action: `SetV4Action` (`:505-520`) — `colorSel` with light index at offset
  6, then up to 3 action phases of 8 bytes each starting at offset 3, stride 8.
- Brightness: `COMMV4_turnOn`, **value is inverted**: `0x64 - brightness` at offset 3,
  max 28 IDs/packet, `:725-744`.
- Power button: 6 control IDs `0x5b..0x60` (AC sleep / AC power / charge / battery
  sleep / battery power / battery critical), each wrapped
  `setPower{4,0,cid}`/`{1,0,cid}`/`{2,0,cid}`, `:630-669`.
- Profile save: control `{4,0,0x61}`, `{1,0,0x61}`, actions, `{2,0,0x61}`, `{6,0,0x61}`, `:595-603`.
- Status codes: `ALIENFX_V4_READY=33 / BUSY=34 / WAITCOLOR=35 / WAITUPDATE=36 / WASON=38`
  (`AlienFX_SDK.h:19-23`) — note a PID `0x551` quirk in `WaitForBusy` (`:849-850`), keep
  it when porting `WaitForBusy`.
- A hard reset packet `{0x2, 0x3, 0xff}` for V4 also appears directly in
  `alienfx-cli/alienfx-cli.cpp:389` (the CLI's `reset` command) — keep this consistent
  with the SDK's own reset sequence when porting the CLI ([07](07-alienfx-cli.md)).

### V5 (64-byte feature reports, internal per-key RGB keyboards, VID `0x0d62`)

`alienfx-controls.h:75-86`:

```cpp
const byte COMMV5_reset[]{ 1, 0x94 };
const byte COMMV5_status[]{ 1, 0x93 };
const byte COMMV5_colorSet[]{ 2, 0x8c, 0x02 };     // [2] can be 1,2,5,6,7,13
const byte COMMV5_loop[]{ 2, 0x8c, 0x13 };
const byte COMMV5_update[]{ 3, 0x8b, 0x01, 0xff };
const byte COMMV5_turnOnSet[]{ 3, 0x83, 0x38, 0x9c };   // [4] brightness
const byte COMMV5_setEffect[]{ 8, 0x80,0x02,0x07,0x00,0x00,0x01,0x01,0x01 };
// [2] type, [3] tempo, [9] ncolors-1, [10..12] RGB1, [13..15] RGB2
// types: 0 color,1 reset,2 breathing,3 single-wave,4 dual-wave,5-7 off,
//        8 pulse,9 mix-pulse,a night-rider,b lazer
```

Color blocks are 4 bytes `{lightID+1, r, g, b}` starting at offset 4, stride 4, packed
until `length` (`AddV5DataBlock` `:372-374`; loop at `:397-407`/`:481-491`), followed by
`COMMV5_loop`. Status: send `COMMV5_status` then `HidD_GetFeature`, value in
`buffer[2]` (`:796-802`); status codes `ALIENFX_V5_STARTCOMMAND=0x8c /
WAITUPDATE=0x80 / INCOMMAND=0xcc` (`AlienFX_SDK.h:25-27`).

### V6 (65-byte interrupt, monitors — VID `0x187c` or `0x0424`)

`alienfx-controls.h:88-105`:

```cpp
const byte COMMV6_systemReset[]{ 4, 0x95,0,0,0 };
const byte COMMV6_colorSet[]{ 2, 0x92, 0x37 };
// [3] command length (a color, b pulse, f morph, 7 timing)
// [6] command (87 color, 88 pulse, 8c morph/breath, 84 timing)
// [8] command type (4 color, 1 morph, 2 pulse, 3 timing)
// [9] light mask
//   4,87 -> [10,11,12] RGB, [13] brightness 0..64, [14] checksum
//   2,88 -> + [14] tempo, [15] checksum
//   1,8c -> [10-12] RGB1, [13-15] RGB2, [16] brightness, [17,18] tempo, [19] checksum
const byte v6OpCodes[]{ 0x87, 0x88, 0x8c,0x8c,0x8c,0x8c,0x8c };
const byte v6TCodes[]{ 4, 2, 1, 1, 1, 1, 1 };
```

Build (`AlienFX_SDK.cpp:42-62`) computes an **XOR checksum** over the sent fields:

```cpp
*mods = { { 9, { (byte)index, c1.r, c1.g, c1.b } } };
byte mask = (byte)(c1.r ^ c1.g ^ c1.b ^ index);
// AlienFX_A_Color: mask ^= 8;                                    push {13,{bright,mask}}
// AlienFX_A_Pulse: mask ^= byte(tempo^1);   {3,{0xb}},{6,{0x88}},{8,{2}},{13,{bright,tempo}},{15,{mask}}
// AlienFX_A_Morph: mask ^= c2.r^c2.g^c2.b^tempo^4; {3,{0xf}},{6,{0x8c}},{8,{1}},{13,{c2 rgb}},{16,{bright,2,tempo,mask}}
```

Buffer is pre-filled `0xff` (see `PrepareAndSend`). **`SetBrightness` is a no-op for
V6/V7**, returning `true` unconditionally (`:752-753`) — dimming for these devices must
be done in software (scale RGB before sending), not hardware.

### V7 (65-byte interrupt write + read, mice — VID `0x0461`)

`alienfx-controls.h:107-113`:

```cpp
const byte COMMV7_update[]{ 8, 0x40,0x60,0x07,0x00,0xc0,0x4e,0x00,0x01 };
const byte COMMV7_status[]{ 5, 0x40,0x03,0x01,0x00,0x01 };
const byte COMMV7_control[]{ 5, 0x40,0x10,0x0c,0x00,0x01 };
// [5] effect mode, [6] brightness, [7] lightID, [8..10] rgb1, [11..13] rgb2, ...
static byte v7OpCodes[]{ 1,5,3,2,4,6,1 };
```

`SetAction` (`:533-541`) patches `{5, {opcode, bright, index}}` plus up to
`(length-10)/3` RGB triplets at `count*3+8`. Every write is followed by a read
(`:128-131`, part of `PrepareAndSend`'s V7 branch) — this is not optional, the device
appears to require the read to complete the transaction.

### V8 (65 bytes, external keyboards — VID `0x04f2`)

`alienfx-controls.h:116-153`:

```cpp
const byte COMMV8_effectReady[]{ 3, 0x5, 0x01, 0x51 };
// [2] chain no (0xff = reset), [3] effect type, [4-6] RGB1, [7-9] RGB2, [10] tempo,
// [11] brightness, [12] chain length, [13] mode (1 permanent, 2 key-press),
// [14] color mode (0/1 one-color, 2 two-color, 3 spectrum)
const byte COMMV8_readyToColor[]{ 4, 0xe, 0x1, 0x0, 0x1 };
// [2] lights in following blocks, [3] profile, [4] packet number within group
// [5] light id, [6] effect (80 off,81 color,82 pulse,83 morph,84 default-blue,
//     87 breath,88 spectrum), [7] tempo, [9] time, [10] brightness,
// [11-13] RGB, [14-16] RGB2, [18] color count
// up to 4 blocks/packet, repeating at [20-33],[35-48],[50-63]
const byte COMMV8_setBrightness[]{ 1, 0x17 };   // [1] brightness 0..0xa
const byte v8OpCodes[]{ 0x81, 0x82, 0x83, 0x87, 0x88, 0x84, 0x81 };
```

`AddV8DataBlock` (`:365-370`) emits 13 bytes: `{index, opcode, tempo, 0xa5, time, 0x0a,
r,g,b, r2,g2,b2, 2}`, stride 15 starting at offset 5 (`:386,:473`), with the packet
counter written at offset 4.

`PrepareAndSend`'s V8 branch chooses transport by a size heuristic, not by command
type: `needV8Feature = mods->front().vval.size() == 1` (`:110`) — single-byte patches
(e.g. `COMMV8_setBrightness`) go via `SET_FEATURE`; multi-byte data blocks go via
interrupt write. **Preserve this heuristic exactly** — it's fragile-looking but is what
real hardware expects; don't "clean it up" into an explicit per-command transport table
without testing against V8 hardware first.

## Global effects (V5/V8 only)

`AlienFX_SDK.cpp:764-785`; `IsHaveGlobal()` (`:1174-1177`) returns true only for V5/V8.
V8 sends `COMMV8_effectReady` twice (bare, then with the 12-byte payload at offset 3),
with a 20ms sleep between. V5 sends either `{2,{1,0xfe}}` (off) or
`{2,{type,tempo}}` + `{9,{ncolors-1, RGB1, RGB2}}`, then `UpdateColors`.

Effect-name/ID tables (UI-facing, `alienfx-gui/ProfilesDialog.cpp:28-45`):

```cpp
ge_names8[] = {"Off","Color or Morph","Pulse","Back Morph","Breath","Spectrum","One key (K)",
  "Circle out (K)","Wave out (K)","Right wave (K)","Default","Rain Drop (K)","Wave",
  "Rainbow wave","Circle wave","Random white (K)"};
ge_names5[] = {"Off","Static","Breathing","Side Wave","Dual Wave","Pulse","Morph","Bounce","Laser","Rainbow"};
const int ge_types8[]{0,1,2,3,7,8,9,10,11,12,13,14,15,16,17,18},
          ge_types5[]{0,1,2,3,4,8,9,10,11,14};
```

`Doc/alienfx-cli.md` already documents the CLI-facing meaning of these — reuse that
doc's wording verbatim for the ported CLI's `--help` text ([07](07-alienfx-cli.md)).

## Mandatory operation order

`Reset() → one or more SetAction/SetMultiColor/SetMultiAction calls → UpdateColors()`.
`Reset` is implicit-if-needed for some call paths (`inSet` flag,
`AlienFX_SDK.cpp:380,467,525`), but only V2–V5 actually *have* separate reset/update
packets — V6/V7/V8 are stateless per-packet, so this ordering constraint is
version-dependent. Any Linux reimplementation must preserve per-version statefulness,
not impose a single state machine on all versions.

## The ACPI light path (API v0) — do not port standalone

`API_ACPI` exists only when `NOACPILIGHTS` is undefined (`AlienFX_SDK.h:6-8,187-191`)
and only works if the v1 fan SDK's kernel driver is present — it's implemented as
`AlienFan_SDK::Lights` inside `alienfan-tools/alienfan-SDK/alienfan-SDK.cpp:507-572`,
not inside `AlienFX_SDK.cpp` itself. It's gated to a synthetic device ID
`0x187c/0xffff` (`AlienFX_SDK.cpp:277-289`) and supports exactly 3 lights, 8-bit color,
via 5 ACPI methods under `\_SB.AMW1`: `SRST` (reset), `ICPC`/`RCPC` (begin/end command),
`SETC(r,g,b,mask)` (color), `SETB(mode,1)` (brightness 0–0xF). Only relevant on Aurora
R6/R7 desktops (`AlienFX-SDK/README.md:16`). `tr1xem/alienfx-linux` marks this path
"Not Planned". Treat it the same way here — a stretch goal after the fan SDK's ACPI
fallback ([05](05-alienfan-sdk-thermal.md)) exists, never a milestone dependency, since
it needs that fallback's ACPI call machinery anyway and serves very few desktop models.

## Devices reference: `devices.csv`

`alienfx-gui/Mappings/devices.csv` (1885 lines) is the closest thing to a device
database in the project — light *names* and keyboard *grid geometry* for 28 known
machines, keyed by VID/PID (decimal in the file). It does **not** replace live
detection (`AlienFXProbeDevice`) — it only supplies human-readable names/positions
after a device is already matched by VID+report-length. See
[06](06-configuration-storage.md) for how this CSV should be carried into the Linux
config format, and [16](16-testing-and-validation.md) for using its VID/PID list as a
hardware validation matrix.
