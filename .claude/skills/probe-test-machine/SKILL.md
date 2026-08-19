---
name: probe-test-machine
description: Generate or refresh Doc/linux_roadmap/local/test-machine.md, a gitignored per-machine coverage report for the Linux port — which HID light-controller API version(s), fan backend, and roadmap subsystems the *current* machine can actually exercise, versus what must stay golden-vector-only. Use when the user asks to document their hardware for the Linux port, asks what this machine can test, asks to regenerate/refresh the test-machine doc, or when CLAUDE.md's Linux port section points here because the file doesn't exist yet.
---

# Probe Test Machine

Derives a coverage report for whatever machine this runs on — it is not a script that
replays one contributor's laptop, it is the *procedure* doc 16's "living compatibility
table" asks for, executed fresh each time against live hardware. Read-only: it inspects
`/sys`, `/dev`, `lsusb`, `lsmod` and kernel config, but never writes to a device or a
sysfs control attribute.

## Before doing anything

Read `Doc/linux_roadmap/16-testing-and-validation.md` in full — this skill exists to
populate the "living compatibility table, separate from this roadmap" it calls for.
Also skim `AlienFX-SDK/AlienFX_SDK/AlienFX_SDK.cpp:178-258` (`AlienFXProbeDevice`) and
`AlienFX-SDK/AlienFX_SDK/AlienFX_SDK.h:98-107` (the `API_*` enum) — the detection table
there is the thing step 4 below re-derives per machine; don't hardcode a copy of it
here, read it live so a future upstream change is picked up automatically.

## Steps

1. **Check whether regeneration is needed.** If `Doc/linux_roadmap/local/test-machine.md`
   already exists, read its recorded kernel version and BIOS version and compare against
   `uname -r` and `cat /sys/class/dmi/id/bios_version`. If both still match, report "still
   current" and stop — do not rewrite. Regenerate only for new hardware, a kernel upgrade,
   a BIOS update, or when the user explicitly asks for a refresh.

2. **Ensure the ignore rule exists.** This file must never be tracked — it documents one
   person's laptop, not the project. Check `git check-ignore -v
   Doc/linux_roadmap/local/test-machine.md`; if it does not resolve, append
   `/Doc/linux_roadmap/local/` to `.git/info/exclude` (never to the tracked `.gitignore` —
   this fork tracks upstream, and a tracked ignore rule becomes a permanent merge-conflict
   surface for no benefit, since the directory is machine-specific by nature). Re-verify
   with `git check-ignore -v` before continuing.

3. **Run the read-only probe.** Execute the block below as one pass. Every command is
   non-mutating. If a command needs `sudo` to be more informative (e.g. reading
   `/proc/config.gz` when it's root-only), ask before elevating rather than silently
   escalating — most of the block works unprivileged.

   ```bash
   cat /sys/class/dmi/id/{sys_vendor,product_name,bios_version}
   uname -r; . /etc/os-release; echo "$PRETTY_NAME"
   lsusb
   for d in /sys/bus/hid/devices/*; do
     echo "== $d"; cat "$d/uevent"; od -An -tx1 "$d/report_descriptor"
   done
   ls -l /dev/hidraw*; getfacl /dev/hidraw* 2>/dev/null
   grep -rl '187c' /etc/udev/rules.d/ /usr/lib/udev/rules.d/ 2>/dev/null
   for h in /sys/class/hwmon/hwmon*; do echo "$h: $(cat $h/name)"; done
   grep -H . /sys/class/platform-profile/*/{name,choices,profile} 2>/dev/null
   lsmod | grep -Ei 'alien|dell|wmi'
   for w in /sys/bus/wmi/devices/*; do
     echo "$(basename $w) driver=$(basename $(readlink $w/driver 2>/dev/null) 2>/dev/null)"
   done
   zcat /proc/config.gz 2>/dev/null | grep -E 'ALIENWARE_WMI|DELL_SMM|HIDRAW|STRICT_DEVMEM'
   modinfo acpi_call >/dev/null 2>&1 && echo "acpi_call available" || echo "acpi_call ABSENT"
   cat /sys/kernel/security/lockdown 2>/dev/null
   id; ls /sys/class/leds/
   for m in hidapi-hidraw libusb-1.0 libudev Qt6Widgets libpipewire-0.3 wayland-client; do
     printf '%-18s %s\n' "$m" "$(pkg-config --modversion $m 2>/dev/null || echo -)"
   done
   echo "session=$XDG_SESSION_TYPE desktop=$XDG_CURRENT_DESKTOP"
   gcc --version | head -1; cmake --version | head -1
   ```

4. **Map every candidate HID device onto the SDK's detection table.** This is the
   analytical core — the step that makes the output more than a hardware inventory. Read
   the live VID set and byte-length cases straight out of
   `AlienFX_SDK.cpp:196-236` (currently `0x187c`, `0x0d62`, `0x0424`, `0x0461`, `0x04f2`;
   re-read rather than assume this list is still current). For each matching HID device
   from step 3's dump:
   - Parse `Report Count` (the `95 xx` item) from the `report_descriptor` hex.
   - **Add 1 for the report-ID byte** to convert to the Windows `OutputReportByteLength`
     convention the detection switch actually uses (skip the +1 only if the descriptor
     already carries an explicit Report ID item, `85 xx`, at the top level — then Linux
     and Windows lengths already agree).
   - Look up the resulting length in the switch and record the API version reached.
   - **If no case matches, that is a defect, not a "no device found" result.** Report it
     explicitly as an off-by-one/undetectable-device finding, quoting the exact bytes and
     the `path:line` of the switch arm that was closest.
   - For the Darfon `0x0d62` family specifically, check whether the descriptor's vendor
     collection (`Usage Page 0xFF89`, `Usage 0xCC`) is the *only* top-level collection or
     shares the interface with a boot-keyboard collection carrying its own Output report.
     If the latter, `OutputReportByteLength == 0` (V5's trigger, `:198-203`) cannot be true
     on Linux for that node — record this as a detection-strategy gap, not as "V5 absent".

5. **Determine the fan-backend verdict**, per `Doc/linux_roadmap/05-alienfan-sdk-thermal.md`
   Backend A discovery: does any `/sys/class/platform-profile/*/name` or
   `/sys/class/hwmon/*/name` read exactly `alienware-wmi` / `alienware_wmi`? If yes,
   Backend A is live — record the `fan*_input`, `fan*_boost`, `temp*_input` values and the
   `choices` string. If no, check `acpi_call` availability for Backend B; if that's also
   absent, record "fan control unsupported on this machine" and flag Backend B as
   *untestable here* (distinct from *not needed* — say which one it is).

6. **Derive the coverage matrix.** For whichever API version(s) step 4 found, read the
   version-gated `switch (version)` blocks in `AlienFX_SDK.cpp` (`Reset`, `UpdateColors`,
   `SetMultiColor`, `SetMultiAction`, `SetAction`, `SaveLightsState`, `SetPowerAction`,
   `SetBrightness`, `SetGlobalEffects`, `GetDeviceStatus`, `WaitForReady`, `WaitForBusy`,
   `IsDeviceReady`) and note, per function, which arm this machine's version reaches and
   which arms (other versions, the ACPI `#ifndef NOACPILIGHTS` blocks, `SavePowerBlock`)
   are dead on this hardware. Cross-reference `16-testing-and-validation.md`'s named risk
   spots (V8 size heuristic, V6 XOR checksum, V2 4-bit packing, V7 write-then-read) and
   state plainly whether this machine's detected version(s) cover any of them. Also check
   `alienfx-gui/Mappings/devices.csv` for an exact VID/PID match to the probed devices —
   if found, its light names and grid geometry become a ready-made assertion fixture; cite
   the line range.

7. **Write `Doc/linux_roadmap/local/test-machine.md`.** Match the house style of the
   numbered roadmap docs: single `#` H1 as `Topic: Subtitle`, `##`/`###` sections (no
   numbering), no emoji, GFM tables with header rows, `path:line` code references in
   backticks, cross-links to sibling docs as `[16](../16-testing-and-validation.md)` (note
   the `../` — this file lives one directory deeper), bold for decisions/warnings. Include
   at minimum: purpose/status (gitignored, one machine, dated), any confirmed detection
   defects from step 4 stated as headline findings with descriptor evidence, machine
   identity/OS/toolchain, lighting hardware findings, fan/thermal verdict, negative results
   (things checked and absent — don't omit these, they save the next session a re-probe),
   the coverage matrix, the exact probe command block for reproduction, a safe local
   test-procedure ordering (read-only → root hidraw open → single write → restore/reset),
   and a closing pointer to `02`/`04`/`05`/`15`/`16`/`17` for what this file intentionally
   does not restate.

8. **Verify.** Confirm every `path:line` reference written into the new doc resolves
   against the current tree (adapt the Python resolution snippet from the
   `update-linux-roadmap` skill's step 7, pointed at
   `Doc/linux_roadmap/local/test-machine.md` in addition to `CLAUDE.md` and the numbered
   docs). Confirm `git status --porcelain` shows nothing new — the generated file must not
   appear as untracked once the `.git/info/exclude` rule is in place.

## Output

Report what was found, leading with any detection defect (undetectable device, wrong API
version, a backend confirmed live vs. confirmed untestable) — that is the material result,
not the file write itself. State the file path, whether it was freshly generated or
confirmed still current, and one line on what the coverage matrix says this machine can and
cannot validate.

## Constraints

- Read-only against hardware and drivers: never write to `/dev/hidraw*`, never write to a
  `fan*_boost`, `pwm*`, or `platform-profile/*/profile` sysfs attribute, never run a light
  or fan command as part of this skill. This skill documents capability, it does not
  exercise it — that's a separate, explicit testing action the user drives themselves.
- Never edit the tracked `.gitignore` — the ignore rule belongs in `.git/info/exclude` only.
- Don't restate protocol tables, udev/polkit design, or milestone sizing that already live
  in `02`/`04`/`05`/`15`/`16`/`17` — link to them instead.
- If a probe command returns nothing or errors (module absent, file missing, permission
  denied), record that as a negative result in the doc. Don't omit the section and don't
  fabricate a value to fill it.
- This is documentation generation, not Linux port implementation — don't write or modify
  port source code as part of this skill.
