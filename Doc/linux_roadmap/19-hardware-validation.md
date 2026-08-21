# Hardware Validation Ledger

## Why this file exists

This is the Linux-side counterpart to
[18-windows-verification.md](18-windows-verification.md): a standing ledger of which
combinations of API version and physical device have actually been exercised against
the Linux port, by whom, on what hardware and kernel version — as opposed to which are
only covered by golden-vector tests ([16-testing-and-validation.md](16-testing-and-validation.md)).
[16](16-testing-and-validation.md)'s hardware validation matrix section has asked for
this table since it was written; `local/test-machine.md:12-15` already calls itself
"that table's first entry". This file is the table.

A row here starts **Untested** the moment M2a's golden-vector tests exist for that
version, and only moves to **Validated** when someone with the matching physical device
runs the safe local test procedure (`local/test-machine.md`'s "Safe local test
procedure" section is the template) and records the result below. Golden-green is not
the same as hardware-validated — that distinction is the entire reason M2 was split
into sub-milestones in the first place (see [17-milestones.md](17-milestones.md)).

The "Golden + transport (M2a/M2b)" column covers two now-done milestones at once: M2a's
golden vectors (packet construction is byte-identical to the pre-port source) and M2b's
replay of those same vectors through the real hidapi-backed transport
(`hid_backend_linux.cpp` against `tests/support/fake_hidapi.cpp`) — neither needs
hardware, and both are done for all 7 versions. Neither substitutes for the
"Hardware-validated" column: M2b's backend has never opened a real device (that's M2c's),
so "transport code is right" and "transport code works against silicon" remain distinct
facts, same principle as M2a vs. M2d for V4.

## Per-version ledger

| API | Device class | Golden + transport (M2a/M2b) | Linux detection | Hardware-validated | Tester / machine | Notes |
|---|---|---|---|---|---|---|
| V2 | Old notebooks (m14x/17x, 13R1/R2) | Done | Code-complete (M2c), detection untested — no device | **Untested** — no known V2 device available to any current contributor | — | Legacy `SavePowerBlock` power-button sequence also untested |
| V3 | Old notebooks | Done | Code-complete (M2c), detection untested — no device | **Untested** | — | Shares detection code with V2 |
| V4 | Modern notebooks/desktops/Aurora R8+ ("tron") | Done | **Done** — Finding 1's off-by-one fixed (`hid_report_descriptor.cpp`'s conditional `+1`); `187c:0550` on the fork's test machine resolves to `API_V4`, confirmed both by `tests/alienfx_sdk/detection_test.cpp`'s named exit-criterion case (parsing the real 25-byte descriptor) and by running `probe_demo` against the live hidraw node | **Untested** — opening the node (as opposed to detecting it) still needs M2d's udev-permission work; `probe_demo` reports `EACCES` on this machine today, with a diagnostic pointing at the udev rule | — | Detection is done; M2d is now unblocked and is the milestone's "prove it end to end, lights on" step — see [17](17-milestones.md) |
| V5 | Internal per-key RGB keyboards (Darfon, VID `0x0d62`) | Done | **No Linux equivalent as written** — Finding 2, `local/test-machine.md:56-87`; needs the M2e redesign. M2c narrowed the remaining gap: hidapi's hidraw backend already yields Windows' per-top-level-collection `usage`/`usagePage` view during enumeration (confirmed live: this machine's `0d62:3740` node reports `usage=0x0001/0x0006`, the boot-keyboard collection, as the first of several `hid_device_info` entries `hid_enumerate()` yields for that one path) — only the per-collection report-*length* half of Finding 2 is still unresolved, since `hid_report_descriptor.cpp` deliberately aggregates report lengths across the whole node (see doc 04's note by Finding 2) | **Untested**. A physical device exists (`0d62:3740` on the test machine, confirmed non-zero aggregate output length via `probe_demo`: `out=2 feat=8`) but `devices.csv:130` marks this exact model "Unused" — validating it may not be worth doing even after M2e lands | — | Feature-report-only detection (`Usage == 0xcc`) is one of doc 16's four fragile spots |
| V6 | Monitors (VID `0x187c` or `0x0424`) | Done | Code-complete (M2c), detection untested — no device | **Untested** — no monitor device available to any current contributor | — | XOR checksum (doc 04) is one of doc 16's four fragile spots; `SetBrightness` is a documented no-op for V6 |
| V7 | Mice (VID `0x0461`, Primax) | Done | Code-complete (M2c), detection untested — no device | **Untested** | — | Write-then-read transport requirement is one of doc 16's four fragile spots |
| V8 | External keyboards (VID `0x04f2`, Chicony, AW410k/510k) | Done | Code-complete (M2c), detection untested — no device | **Untested** | — | Feature-vs-interrupt size heuristic is one of doc 16's four fragile spots |

## How to add a validated row

Follow `local/test-machine.md`'s "Safe local test procedure": confirm the device via the
read-only probe block first, send one small reversible command, know the V4 reset
sequence (or the version-appropriate equivalent) before trying anything else if the
device stops responding. Then fill in this table: which specific operations were
exercised (not just "it turned on" — note whether `SetMultiColor`, `SetAction` phases,
`SetPowerAction`, and brightness were each tried), the tester, the machine (model + BIOS
+ kernel, mirroring `local/test-machine.md`'s "Machine identity" table), and the date.
A row with a documented failure is still more useful than an unfilled row — it tells the
next person what's already known to be broken, same principle as
[18](18-windows-verification.md).

## Relationship to `local/test-machine.md`

`Doc/linux_roadmap/local/test-machine.md` is gitignored and machine-specific — it
documents everything one contributor's one machine can and cannot exercise, regenerated
by the `probe-test-machine` skill. This file is the opposite: tracked, aggregated across
every contributor's hardware, and organized by API version rather than by machine. When
`probe-test-machine` surfaces a new finding (a new detection bug, a new fragile
behavior), that finding belongs in `local/test-machine.md` and/or
[04-alienfx-sdk-hid.md](04-alienfx-sdk-hid.md); a *validation result* — "I plugged in
device X and operation Y worked" — belongs here instead.
