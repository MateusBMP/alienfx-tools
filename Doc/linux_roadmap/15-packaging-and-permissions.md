# Packaging and Permissions

## udev rules for HID light devices

Non-root users need read/write access to the hidraw device nodes for every VID this
project talks to ([04](04-alienfx-sdk-hid.md)):

| VID | Vendor |
|---|---|
| `0x187c` | Alienware |
| `0x0d62` | Darfon |
| `0x0424` | Microchip |
| `0x0461` | Primax |
| `0x04f2` | Chicony |

Ship a udev rules file granting a dedicated group (e.g. `alienfx`) access to matching
hidraw nodes, on the same model
[`trackmastersteve/alienfx`](https://github.com/trackmastersteve/alienfx) already
uses in production (its README documents shipping a udev rules file specifically so
the tool "does not need root permissions" for HID access) — reuse that project's rule
structure as a starting reference rather than designing from scratch, since it's
already field-tested against this hardware family. Package post-install should:
create the group if absent, install the rules file to
`/etc/udev/rules.d/`, and prompt (or `udevadm trigger`) so the rule applies without a
reboot. Document that the user must be added to the group and re-login (group
membership doesn't apply to already-open sessions).

## Kernel requirements for fan control

- **Backend A** ([05](05-alienfan-sdk-thermal.md)): needs Linux **6.13+** for
  `alienware-wmi-wmax` hwmon/fan-boost support (thermal-profile support alone landed
  slightly earlier). Document the minimum kernel version prominently — this is a hard
  requirement, not a "recommended," since the sysfs nodes simply don't exist on older
  kernels. For models outside the driver's allowlist, document the
  `force_platform_profile=1`/`force_hwmon=1`/`force_gmode` module parameters
  (`modprobe alienware-wmi force_hwmon=1`, or a `/etc/modprobe.d/` config for
  persistence) as the escape hatch.
- **Backend B** (fallback): needs the out-of-tree `acpi_call` kernel module, which is
  not in-tree and must be installed separately — typically via DKMS so it survives
  kernel updates (available in most distro repos, e.g. `acpi_call-dkms` on
  Debian/Ubuntu, AUR `acpi_call-dkms` on Arch). Document this as an optional
  dependency, only required if the user's model isn't covered by Backend A.

## Privileged operations: polkit over setuid

For any write path that needs elevated privileges beyond what a udev rule can grant
(fan boost writes where the hwmon node isn't group-writable, `acpi_call` access), use a
polkit action invoked by `alienfxd` ([09](09-daemon-and-monitor.md)) rather than
running the whole daemon as root or shipping a setuid binary. A polkit action can be
scoped precisely ("this one D-Bus method needs authentication") and integrates with
each desktop environment's native auth prompt, whereas a setuid binary has a larger
attack surface for what is otherwise a session-scoped user tool. Only fall back to a
narrowly-scoped setuid/`cap_sys_rawio`-capable helper binary if polkit integration
proves impractical for a specific write path at implementation time.

## A cautionary example from a different tool: issue #524

Upstream issue [#524](https://github.com/T-Troll/alienfx-tools/issues/524) ("Initial
setup failed because path for udev is not really a path") is not actually about this
project — a user pasted a Python `setup.py` traceback from a different (unrelated)
Linux tool into this project's issue tracker by mistake, and the maintainer correctly
identified it as belonging to a different repository. It's referenced here only as a
real-world example of the failure mode to avoid: a `TypeError` when a script asserts a
path exists in the wrong type/form during udev-rule installation. Whatever installer
this project ships (package post-install script, or a `Doc/alienfx-config`-style shell
script analogous to the existing `alienfx-config.cmd`) should validate paths explicitly
and fail with a clear message rather than a raw traceback, and should be tested against
a real target distro before release, not just eyeballed.

## Packaging targets

- **AUR (Arch)**: straightforward given CMake + standard dependencies
  ([02](02-build-system.md)); likely the fastest path to a real user base given the
  Alienware/gaming-laptop user overlap with Arch-based distros.
- **`.deb`/`.rpm`**: standard CMake-based packaging (`cpack` or native distro
  packaging scripts) once dependency versions are pinned per distro.
- **Flatpak: a poor fit, document why rather than attempting it.** This project's core
  functionality depends on direct hidraw device access, sysfs read/write, and (for
  [13](13-input-and-hotkeys.md)) raw `/dev/input` access — all things Flatpak's
  sandbox model is specifically designed to restrict or mediate through portals that
  don't exist for these use cases. A Flatpak build would either need broad
  `--filesystem=host`/`--device=all` overrides that defeat the sandbox's purpose, or
  simply not work. Skip it; note this explicitly in user-facing docs so it isn't
  requested repeatedly.

## Data files

`alienfx-gui/Mappings/devices.csv` ships as a read-only data file
(see [06](06-configuration-storage.md)) — install to the distro's standard shared-data
location (e.g. `/usr/share/alienfx-tools/devices.csv`) via CMake's `install(FILES ...)`,
and have the CSV loader search `$XDG_DATA_DIRS` plus that fixed path, matching XDG
convention rather than hardcoding a single install prefix.
