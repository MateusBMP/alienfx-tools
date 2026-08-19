# Research Sources and Provenance

This doc exists so a future update to this roadmap is an **increment**, not a
re-investigation. Every external source consulted while writing docs `01`–`17`, and
every local file deeply analyzed, is logged here with what was extracted and which
roadmap doc(s) consumed it. Before re-fetching anything, check this list first —
either the fact is already here, or the "how to extend" section at the bottom tells
you exactly what to check for staleness.

**Pinned to**: this fork's `master` @ commit `52713b2`, research performed
**2026-08-19**.

## A. Upstream repository — `T-Troll/alienfx-tools` issues & discussions

Search performed: GitHub API `search/issues?q=repo:T-Troll/alienfx-tools+linux`,
which returned 13 results at time of research. Of those, four were read in full
(title, body, all comments) because they directly address Linux support intent or
porting mechanics; the rest were triaged by title only (listed at the end of this
section).

| # | Title | State | Read in full? | Key extraction | Feeds |
|---|---|---|---|---|---|
| [Discussion #421](https://github.com/T-Troll/alienfx-tools/discussions/421) | "Will you add Linux support?" | open | yes | Maintainer: *"I use Linux for my main duty, but WSL enough for me... So no, Linux support is not planned."* Also: SDK v2 fan control is WMI/Windows-specific; SDK v1 uses direct ACPI, model-tuning needed. Follow-up on game-mode via `rwdec`-assisted ACPI dumps. | `01` |
| [#434](https://github.com/T-Troll/alienfx-tools/issues/434) | "Tool for linux" | open | yes, incl. all comments through 2025-11-01 | The single most substantial thread. Maintainer's full porting checklist (retype Windows ints, port `AlienFXCheckDevice`/`AlienFXInitialize`, port `PrepareAndSend` to Linux HID). Confirms HID uses Report/Feature/Interrupt all three. `rwdec` tool introduced and explained (ACPI/BIOS `.rw` dump decoder, finds `AWCCWmiMethodFunction` mapping). Kernel driver author `kuu-rt` participated directly, cross-referencing this project's SDK v2 notes; described the kernel's per-model allowlist necessity and `force_platform_profile`/`force_gmode` params. `tr1xem` (later `alienfx-linux`/`AWCC` author) joined 2025-11-01, discussed `AMW3`/`AMWW`/`AMW1` prefix variance and per-key RGB zone mapping. | `01`, `05` |
| [#272](https://github.com/T-Troll/alienfx-tools/issues/272) | "cli for linux?" | closed | yes, incl. all comments | Maintainer's earlier, shorter porting checklist (Windows types → Linux, HID detection, `PrepareAndSend`, `Mappings` off registry to `/etc` config). Note: some AW keyboards report as Alienware **or** Chicony **or** Darfon VID. | `01`, `04` |
| [#255](https://github.com/T-Troll/alienfx-tools/issues/255) | "Any Plans for Linux Support?" | closed | yes, incl. all comments | Same "not planned, but source is open, quite easy for CLI" answer, earliest instance found (2022-11-29). | `01` |
| [#524](https://github.com/T-Troll/alienfx-tools/issues/524) | "Initial setup failed because path for udev is not really a path" | closed | yes, incl. all comments | Turned out to be a misfiled report against an unrelated Python tool — maintainer redirected the user. Used only as a cautionary example of a udev/setup failure mode to avoid. | `15` |

Titles seen in the search but **not** opened (triage by title suggested lower direct
relevance to the "why no Linux support" / porting-mechanics question this roadmap
needed; revisit if a future pass needs hardware-specific detail):
[#587](https://github.com/T-Troll/alienfx-tools/issues/587) "alienware m18r1, not
detecting bottom row keys except left ctrl" (open),
[#359](https://github.com/T-Troll/alienfx-tools/issues/359) "M16 r1 Alien FX not
detected" (open), [#607](https://github.com/T-Troll/alienfx-tools/issues/607) "9.4.1.1
installer detected as Trojan" (closed, AV false-positive, matches the root
`README.md`'s "Security and privacy" section — not Linux-related),
[#461](https://github.com/T-Troll/alienfx-tools/issues/461) "AW3423DWF not detected"
(closed), [#315](https://github.com/T-Troll/alienfx-tools/issues/315) "Max Fan Boost
Power Level N vs Manual Mode 100%" (closed), [#127](https://github.com/T-Troll/alienfx-tools/issues/127)
"Dell G15 5515 Ryzen Edition Fan Control Not Working" (closed),
[#42](https://github.com/T-Troll/alienfx-tools/issues/42) "I could't make this work
using python" (closed), [#29](https://github.com/T-Troll/alienfx-tools/issues/29)
"Dell G7 7500 laptop sorta works, sorta doesn't" (closed),
[#4](https://github.com/T-Troll/alienfx-tools/issues/4) "Question" (closed).

## B. Linux kernel documentation

| Source | Accessed for | Key extraction | Feeds |
|---|---|---|---|
| [docs.kernel.org/admin-guide/laptops/alienware-wmi.html](https://docs.kernel.org/admin-guide/laptops/alienware-wmi.html) | sysfs ABI | `/sys/class/platform-profile/*` (name `alienware-wmi`, `choices`/`profile`); `/sys/class/hwmon/*` (name `alienware_wmi`, `fan[1-4]_input`, `fan[1-4]_boost` 0–255, `temp*_input`); `pwm = pwm_base + (boost/255)·(pwm_max−pwm_base)`; "custom profile required for reliable manual fan control" caveat; module params `force_platform_profile=1`, `force_hwmon=1`, `force_gmode` | `05`, `15` |
| [docs.kernel.org/wmi/devices/alienware-wmi.html](https://docs.kernel.org/wmi/devices/alienware-wmi.html) | WMI/ACPI method internals | WMAX GUID `{A70591CE-A997-11DA-B012-B622A1EF5492}`; two implementations (legacy LED/HDMI/amp vs. newer thermal/overclock); method IDs 20 (`Thermal_Information`)/21 (`Thermal_Control`)/37 (`GameShiftStatus`) with operation sub-codes; legacy thermal profile codes `0x96–0x99`, USTT `0xA0–0xA5`, G-Mode `0xAB`; GPIO methods 32–34 (DFU pin 0, STM32 RGB controller reset pin 1, VID `187c`) | `05` |

## C. Related/prior-art Linux projects

| Project | What was fetched | Key extraction | Feeds |
|---|---|---|---|
| [tr1xem/alienfx-linux](https://github.com/tr1xem/alienfx-linux) | Repo overview (WebFetch); file tree via `api.github.com/repos/tr1xem/alienfx-linux/git/trees/HEAD?recursive=1`; raw content of `CMakeLists.txt` (root + `AlienFX-SDK/` + `AlienFan-SDK/`), `AlienFX-SDK/include/libusb_helper.h`, `AlienFX-SDK/src/libusb_helper.cpp` (first 100 lines), `AlienFan-SDK/include/AlienFan-SDK.h` (first 90 lines), `AlienFan-SDK/src/AlienFan-SDK.cpp` (first 120 lines), `README.md` | Working CMake+hidapi/libusb port of both SDKs, by an `#434` participant. `libusb_helper.cpp` shims `HidD_SetFeature`/`HidD_SetOutputReport`/`WriteFile`/`ReadFile`/`HidD_GetFeature`/`HidD_GetInputReport` over hidapi — the exact "reimplement the six Windows function names" technique recommended in `04`. `AlienFan-SDK.cpp` walks `/sys/class/hwmon` and `/sys/class/platform-profile` by reading each `name` file looking for `alienware_wmi`/`alienware-wmi` — the exact discovery procedure documented in `05`. README documents supported API versions v2–v8 (v0 "Not Planned", v1 "deprecated and removed") and lists FetchContent deps (libusb-cmake, hidapi, loguru, nlohmann_json). | `01`, `02`, `04`, `05` |
| [tr1xem/AWCC](https://github.com/tr1xem/AWCC) | Repo overview (WebFetch) | Unofficial AWCC alternative by the same author; daemon-first architecture (~4MB headless, ~88MB with GUI); uses `acpi_call` + the kernel `alienware-wmi` driver; D-Bus-adjacent daemon/GUI split. Used as the architectural reference for the daemon-centric design in `09`/`10`. **Not deeply code-read** — only the repo description was fetched, no source files. | `09`, `10` |
| [trackmastersteve/alienfx](https://github.com/trackmastersteve/alienfx) | Identified via `WebSearch` only, title/snippet read | Python CLI+GTK tool, ships udev rules so it needs no root for HID access. **Not fetched in depth** — referenced in `15` for its udev-rule precedent based on the search snippet alone. Worth a real read before implementing `15`. | `15` |
| [rsm-gh/akbl](https://github.com/rsm-gh/akbl) | Identified via `WebSearch` only, title/snippet read | Older GTK Alienware light controller. **Not fetched in depth.** Mentioned in `01` as prior art. | `01` |
| [T-Troll/rwdec](https://github.com/T-Troll/rwdec) | **Never fetched directly** — only referenced because the maintainer links/describes it repeatedly inside issue #434 and discussion #421 | ACPI/BIOS `.rw`-dump decoder that finds which ACPI device (`AW**`) maps to `AWCCWmiMethodFunction`, used for per-model ACPI method discovery. Everything this roadmap says about it is second-hand from the maintainer's own description in the issue threads, not from reading the tool's source. | `05`, `16` |

## D. Local repository files analyzed (this fork, commit `52713b2`)

Gathered via two parallel deep-dive explorations (Windows-API-dependency inventory and
hardware-protocol-layer inventory) plus direct reads. File-by-file findings are already
baked into docs `03`–`06`, `09`–`14` as `path:line` citations — this table records
*which files were actually opened*, so a future pass knows what's already covered
versus what would be new ground (e.g. if a new device family's source file appears).

| Area | Files read | Feeds |
|---|---|---|
| Light SDK | `AlienFX-SDK/AlienFX_SDK/AlienFX_SDK.cpp` (1177 l), `AlienFX_SDK.h` (340 l), `alienfx-controls.h` (172 l), `AlienFX_SDK.vcxproj`/`AlienFX_SDK_noACPI.vcxproj` | `04` |
| Fan SDK v1 | `alienfan-tools/alienfan-SDK/alienfan-SDK.cpp` (573 l), `.h` (203 l), `alienfan-controls.h` (95 l), `alienfan-low/alienfan-low.c` (869 l), `.h` (96 l), `Ioctl.h` (621 l), `packages.config` ×2 | `05` |
| Fan SDK v2 | `alienfan-tools/alienfan-SDK_v2/alienfan-SDK.cpp` (391 l), `.h` (160 l), `alienfan-controls.h` (76 l) | `05` |
| Shared/common | `Common/Common.cpp` (315 l), `common.h` (36 l), `CustomMutex.h/.cpp` (16/33 l), `ThreadHelper.h/.cpp` (15/47 l), `RegHelperLib/RegHelperLib.cpp/.h` (24/8 l) | `03`, `06`, `09` |
| GUI | all 26 files under `alienfx-gui/` (~9099 l total incl. `kiss_fft/`), `alienfx-gui.rc` (839 l, 22 `DIALOGEX` blocks), `Mappings/devices.csv` (1885 l) | `04`, `06`, `09`–`13` |
| CLIs/mon | `alienfx-cli/alienfx-cli.cpp` (410 l) + `Consts.h` (75 l); `alienfx-mon/alienfx-mon.cpp` (664 l), `SenMonHelper.cpp/.h` (243/45 l), `ConfigMon.cpp/.h` (96/88 l), `alienfx-mon.rc` (244 l); `alienfan-tools/alienfan-cli/alienfan-cli.cpp` (376 l); `alienfan-tools/alienfan-gui/alienfan-gui.cpp` (580 l), `resource.h` (57 l), `.rc` (240 l) | `07`, `08`, `09` |
| Shared `.vcxitems` | `alienfan-tools/alienfan-shared/ConfigFan.cpp/.h` (259/89 l), `alienfan-tools/alienfan-mon/MonHelper.cpp/.h` (248/38 l), `alienfan-tools/alienfan-curve/FanCurve.cpp` (481 l) | `06`, `09`, `10` |
| LightFX | `LightFX/LightFX.cpp` (390 l), `LFX2.h` (438 l), `LFXDecl.h` (237 l), `dllmain.cpp` (28 l), `LightFX.rc` (109 l); `alienfx-LFX/LFXUtil.cpp` (232 l), `.h` (54 l), `LFX2.h`/`LFXDecl.h` (duplicate copies), `LFXConfigurator.h` (205 l) | `14` |
| Build system | `alienfx-tools.sln` (25 entries), `AlienFX-SDK/AlienFX_SDK.sln`, all `.vcxproj`/`.vcxitems` (checked for `AdditionalDependencies` and `#pragma comment(lib` cross-reference), `Install/Install.vdproj` (1422 l, noted out of scope) | `02` |
| Project docs | root `README.md` (full), `Doc/alienfx-cli.md` (full, 45 l) | `01`, `07` |
| Git history | `git log --oneline` (992 commits), `git log --format='%an %ae'` (author list) | context only, not cited |

**Not read**: `Doc/alienfx-gui.md`, `Doc/alienfan-cli.md`, `Doc/alienfan-gui.md`,
`Doc/alienfx-mon.md`, `Doc/LightFX.md` (the other user-facing doc files) —
`Doc/alienfx-cli.md` was the only one opened, because `07` needed its exact command
semantics. A future pass on `08`/`09`/`10`/`14` should read the matching doc file
first, the same way `07` did, rather than re-deriving CLI/GUI semantics from source
alone.

## E. Web searches performed

| Query | Purpose |
|---|---|
| `alienfx-tools T-Troll Linux support issue` | Initial orientation before the targeted issue-search API call |
| `Linux kernel alienware-wmi wmax driver AlienFX lights fan control hwmon` | Found the two kernel doc pages in section B |
| `Linux AlienFX USB HID lights control open source project hidapi Alienware keyboard` | Found the four prior-art projects in section C |

## How to extend this research incrementally

When revisiting this roadmap in the future, don't redo the searches above — instead:

1. **Check for new upstream activity since 2026-08-19**: re-run
   `search/issues?q=repo:T-Troll/alienfx-tools+linux` (or the `gh` CLI equivalent,
   `gh search issues --repo T-Troll/alienfx-tools linux`) and diff the result set
   against section A's table — only read issues/comments *newer* than what's logged
   here, or ones whose number isn't in the table at all.
2. **Check discussion #421 and issue #434 for new comments** — both were open/ongoing
   at research time and are the two threads most likely to accumulate new porting
   progress from other contributors.
3. **Check kernel driver progress**: the `alienware-wmi-wmax` driver was mid-development
   (HWMON support, manual fan control) around the research date — re-check
   [docs.kernel.org/admin-guide/laptops/alienware-wmi.html](https://docs.kernel.org/admin-guide/laptops/alienware-wmi.html)
   for ABI additions (new sysfs attributes, expanded model allowlist, changed module
   parameters) before treating `05`'s Backend A description as current.
4. **Check `tr1xem/alienfx-linux` and `tr1xem/AWCC` commit activity** — both were
   active development-stage projects at research time, not finished references. If
   either has progressed materially, re-fetch and update the affected roadmap docs
   (`04`/`05` for `alienfx-linux`, `09`/`10` for `AWCC`) rather than treating section C's
   snapshot as still accurate.
5. **Read `trackmastersteve/alienfx` and `rsm-gh/akbl` in depth** before finalizing
   `15`/`01` respectively — both were only search-snippet-level reviewed here, which is
   a known gap, not a completed review.
6. **If local source files change** (new device API version, new SDK variant, GUI
   refactor), update the specific roadmap doc(s) listed in section D's "Feeds" column
   for that file — don't re-run the full two-agent exploration that originally produced
   this roadmap; targeted re-reads of the changed files are sufficient.
7. **Add new rows to this file** for anything newly fetched, following the same table
   format, so the next pass after yours has the same head start this one is providing
   you.
