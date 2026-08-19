# Testing and Validation

## Starting point: no test suite exists

There is no unit or integration test infrastructure anywhere in this repository today
— no test project in `alienfx-tools.sln`, no test framework dependency. The Linux port
is a natural point to introduce testing discipline, both because new code is being
written from scratch (the config backend, the fan backend) and because the existing
protocol logic being reused ([04](04-alienfx-sdk-hid.md)) is exactly the kind of
byte-layout code that benefits most from golden-value tests.

## Packet-builder unit tests using golden byte vectors

`AlienFX_SDK`'s offset-patch model ([04](04-alienfx-sdk-hid.md)) makes this cheap:
every command is `template bytes + a list of (offset, bytes) patches`, and
`PrepareAndSend` deterministically produces a final buffer from those inputs. Capture
golden output buffers from the **existing Windows build** (instrument
`PrepareAndSend` to dump `buffer[0..length)` before it hits `HidD_SetFeature`/etc., for
a representative call of each API version's each command type) and assert the ported
Linux code produces byte-identical output for the same logical calls. This validates
the port without needing hardware for every test run, and pins down the exact
behaviors flagged as fragile in [04](04-alienfx-sdk-hid.md) (the V8
feature-vs-interrupt size heuristic, the V6 XOR checksum, the V2 4-bit color packing).

Do the same for [05](05-alienfan-sdk-thermal.md)'s Backend B ACPI call encoding (the
4-byte `{sub, arg1, arg2, 0}` buffer) — golden values are easy to produce since the
existing v1 SDK's `RunMainCommand` escape hatch can dump the exact buffer it sends for
known operations.

## A `--dry-run` transport for manual testing without hardware

Add a build-time or runtime option where the HID/ACPI transport layer prints the
packet it *would* send (hex dump, with a human-readable decode of known fields) instead
of writing to the device. This is useful independent of the golden-vector tests above:
it lets a contributor without the specific hardware model in question sanity-check
that their change produces a plausible packet, and lets a hardware owner without dev
tooling capture what a specific CLI invocation actually sends for a bug report.

## Hardware validation matrix

`alienfx-gui/Mappings/devices.csv` ([04](04-alienfx-sdk-hid.md),
[06](06-configuration-storage.md)) already encodes 28 known machine configurations and
~22 distinct VID/PID pairs across 5 vendors — treat this as the hardware coverage
matrix for release validation. It won't be feasible to physically test every entry
before every release; instead:

- Maintain a living compatibility table (separate from this roadmap, updated as the
  port progresses) recording which entries have been validated against the Linux
  build, by whom, and on what kernel version (relevant for the fan-backend allowlist
  question in [05](05-alienfan-sdk-thermal.md)).
- Prioritize validation coverage across API *versions* (v2 through v8) over exhaustive
  per-model coverage within a version — a working V4 implementation on one m-series
  notebook is strong evidence for every other V4 device, but doesn't validate V5's
  feature-report-only detection quirk or V7's write-then-read requirement.
- Recruit testers the way upstream issue #434 organically did — contributors who
  already own specific hardware and are motivated to get their own machine working are
  the realistic source of coverage, not the roadmap author personally acquiring 28
  laptops.

## USB traffic capture procedure for new/unclear devices

For any device where the existing protocol tables in
[04](04-alienfx-sdk-hid.md)/[05](05-alienfan-sdk-thermal.md) don't obviously apply
(new hardware released after this roadmap was written, or an ambiguous VID/PID not in
`devices.csv`):

1. On the Windows build: capture USB traffic with Wireshark's `USBPcap` capture
   interface, or `usbmon` if dual-booting Linux and running the *existing Windows*
   AWCC/this-tool's Windows build under a VM with USB passthrough (less reliable —
   prefer bare-metal Windows + Wireshark/USBPcap for ground truth).
2. On the Linux side once a candidate implementation exists: `usbmon` (via
   `cat /sys/kernel/debug/usb/usbmon/0u` or Wireshark's native Linux USB capture
   support) to compare actual outgoing packets against the golden vectors from Windows.
3. For ACPI-path fan control specifically, use T-Troll's
   [`rwdec`](https://github.com/T-Troll/rwdec) against a BIOS/ACPI dump (`acpidump`
   on Linux, `RWEverything` on Windows) to find the model's `WMAX` method mapping,
   exactly as documented in [05](05-alienfan-sdk-thermal.md) and demonstrated live in
   upstream issue #434's exchange between `urbanze` and `T-Troll`.

## CI

Once a CMake build exists ([02](02-build-system.md)), add CI building the Linux target
with both GCC and Clang (catches the MSVC-extension issues cataloged in
[03](03-platform-abstraction.md) early — anonymous struct-in-union, calling
conventions, etc., since GCC and Clang differ in which MS extensions they tolerate and
how). Keep a CI job (or at minimum a documented manual check) confirming the existing
MSVC `.sln` build still succeeds — this roadmap's explicit constraint is that Linux
work must not break Windows ([README.md](README.md)'s decisions,
[02](02-build-system.md)'s "what NOT to do").
