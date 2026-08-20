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

## Per-version ledger

| API | Device class | Golden vectors (M2a) | Linux detection | Hardware-validated | Tester / machine | Notes |
|---|---|---|---|---|---|---|
| V2 | Old notebooks (m14x/17x, 13R1/R2) | Pending M2a | Not started (M2c) | **Untested** — no known V2 device available to any current contributor | — | Legacy `SavePowerBlock` power-button sequence also untested |
| V3 | Old notebooks | Pending M2a | Not started (M2c) | **Untested** | — | Shares detection code with V2 |
| V4 | Modern notebooks/desktops/Aurora R8+ ("tron") | Pending M2a | **Broken** — Finding 1, `local/test-machine.md:28-54`; fix tracked in M2c | **Untested**, but a real device exists: `187c:0550` on the fork's test machine (Alienware m15 R6). Blocked on M2c's fix, then M2d's udev-permission work | — | Once unblocked, this is the milestone's "prove it end to end" version — see M2d in [17](17-milestones.md) |
| V5 | Internal per-key RGB keyboards (Darfon, VID `0x0d62`) | Pending M2a | **No Linux equivalent as written** — Finding 2, `local/test-machine.md:56-87`; needs the M2e redesign | **Untested**. A physical device exists (`0d62:3740` on the test machine) but `devices.csv:130` marks this exact model "Unused" — validating it may not be worth doing even after M2e lands | — | Feature-report-only detection (`Usage == 0xcc`) is one of doc 16's four fragile spots |
| V6 | Monitors (VID `0x187c` or `0x0424`) | Pending M2a | Not started (M2c) | **Untested** — no monitor device available to any current contributor | — | XOR checksum (doc 04) is one of doc 16's four fragile spots; `SetBrightness` is a documented no-op for V6 |
| V7 | Mice (VID `0x0461`, Primax) | Pending M2a | Not started (M2c) | **Untested** | — | Write-then-read transport requirement is one of doc 16's four fragile spots |
| V8 | External keyboards (VID `0x04f2`, Chicony, AW410k/510k) | Pending M2a | Not started (M2c) | **Untested** | — | Feature-vs-interrupt size heuristic is one of doc 16's four fragile spots |

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
