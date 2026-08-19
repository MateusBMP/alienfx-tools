# GUI Port: Qt 6

## Scale

`alienfx-gui/` is ~9,100 lines of C++ across 26 `.cpp`/`.h` files, plus an 839-line
`.rc` resource script defining 22 `DIALOGEX` blocks, plus 4 `.ico` files. Per-file
sizes (largest first): `alienfx-gui.cpp` 862, `FXHelper.cpp` 810, `DevicesDialog.cpp`
739, `ConfigHandler.cpp` 609, `ProfilesDialog.cpp` 578, `TabDevGrid.cpp` 481,
`EventHandler.cpp` 382, `DXGIManager.cpp` 379, `HapticsDialog.cpp` 286,
`SysMonHelper.cpp` 279, `WSAudioIn.cpp` 270, `ZoneSel.cpp` 269, `ConfigHandler.h` 263,
`EventDialog.cpp` 257, `GridEffectDialog.cpp` 240, `CaptureHelper.cpp` 237,
`SettingsDialog.cpp` 235, `ColorDialog.cpp` 231, `FanDialog.cpp` 217, `LightsDialog.cpp`
189, `GridHelper.cpp` 183, `TabColorGrid.cpp` 178, `AmbientDialog.cpp` 173,
`KeyPressDialog.cpp` 49. The codebase is **183× `GetDlgItem`, 88× `SendMessage`** —
almost every file is a `DLGPROC` state machine with no mechanical translation to a
different UI framework. Budget this as the largest single milestone in the roadmap
(see [17](17-milestones.md)).

## Recommended architecture: GUI as thin client over the daemon

Rather than re-porting 9k lines of dialog logic 1:1, build the Qt GUI as a client of
`alienfxd` ([09](09-daemon-and-monitor.md)) over D-Bus wherever possible: light state,
fan/profile state, and sensor readings are all already owned by the daemon. This means
the GUI doesn't need its own copy of `AlienFX_SDK`/`AlienFan_SDK` device-handle
ownership — it sends commands and receives state updates. This mirrors the
`tr1xem/AWCC` architecture and avoids two processes fighting over the same HID device
handle (a real risk if both a standalone CLI invocation and a resident GUI try to hold
the device open simultaneously — Windows sidesteps this because `alienfx-cli` opens
and closes the handle per-invocation, but a resident GUI historically hasn't needed
to share with anything else).

## Dialog → Qt widget mapping

| Windows dialog/file | Purpose | Proposed Qt equivalent |
|---|---|---|
| `alienfx-gui.cpp` (main, `IDD_MAINWINDOW`) | Tab container, tray, hotkeys, power/device-change events | `QMainWindow` + `QTabWidget`, `QSystemTrayIcon` |
| `DevicesDialog.cpp` | Device list, probing, CSV mapping editor | `QTreeView`/`QTableView` over a device model |
| `LightsDialog.cpp` | Per-light color assignment | Custom `QWidget` list, reusing the grid renderer below |
| `TabDevGrid.cpp`, `TabColorGrid.cpp`, `GridHelper.cpp`, `ZoneSel.cpp` | Visual keyboard/device grid for zone selection | `QGraphicsView`/`QGraphicsScene` or a custom-painted `QWidget` (`QPainter` — see GDI note below) |
| `ColorDialog.cpp` (`ChooseColor`) | Color picker | `QColorDialog` |
| `GridEffectDialog.cpp` | Hardware effect selection (pulse/morph/spectrum/etc.) | `QWidget` form driven by the `ge_types5`/`ge_types8` tables ([04](04-alienfx-sdk-hid.md)) |
| `AmbientDialog.cpp` | Screen-capture ambient lighting config | `QWidget` form; capture backend is [11](11-ambient-capture.md) |
| `HapticsDialog.cpp` | Audio-reactive lighting config | `QWidget` form; audio backend is [12](12-audio-haptics.md) |
| `EventDialog.cpp`, `KeyPressDialog.cpp` | Hotkey/keypress binding UI | `QKeySequenceEdit`-based, backed by [13](13-input-and-hotkeys.md) |
| `ProfilesDialog.cpp` | Profile list, triggers, per-profile effect/fan/power config | `QListView` + a details pane |
| `FanDialog.cpp` | Fan curves, boost, power mode | Custom curve-editing widget (see `FanCurve.cpp` in [08](08-alienfan-cli.md)'s scope) |
| `SettingsDialog.cpp` | App-wide preferences | `QDialog` form |
| `EventHandler.cpp`, `SysMonHelper.cpp` | Foreground-app detection, sensor polling for triggers | Not GUI at all — this logic *moves into the daemon* per [09](09-daemon-and-monitor.md); the GUI only renders what the daemon reports. |

`alienfx-gui.rc`'s 22 `DIALOGEX` blocks give the authoritative control layout/tab
order for each of the above — read the `.rc` alongside its `.cpp` when porting a given
dialog, since control IDs (`resource.h`) are the only link between them and there's no
automatic translation tool for this step.

## Specific Win32 mechanisms and their Qt replacement

- **Tray icon**: `Shell_NotifyIcon(NIM_ADD/MODIFY/DELETE)` calls throughout
  `alienfx-gui.cpp` → `QSystemTrayIcon`. Caveat: tray icon support is inconsistent
  across Wayland compositors (GNOME requires an extension; KDE/Plasma and most
  wlroots-based compositors support the StatusNotifierItem protocol natively via
  `QSystemTrayIcon`). Detect absence gracefully (fall back to "no tray, main window
  stays open" rather than a silent no-op) rather than assuming tray presence.
- **GDI drawing** (`BitBlt`, `AlphaBlend`/msimg32, `CreateCompatibleDC/Bitmap`,
  `SelectObject`, `CreateSolidBrush`, `FillRect`, `DrawText`, `BeginPaint/EndPaint`,
  `CreateIconIndirect`, `LoadImage`) used throughout the grid/dialog rendering code →
  `QPainter` on a `QWidget::paintEvent` or a `QPixmap`-backed custom widget. This is a
  straightforward but large mechanical rewrite — there is no automatic GDI→QPainter
  translator, budget real time for each grid/color widget individually.
- **`ChooseColor`** (`<ColorDlg.h>`) → `QColorDialog::getColor`.
- **`GetOpenFileName`** → `QFileDialog::getOpenFileName`.
- **`TrackPopupMenu`/`LoadMenu`** → `QMenu::exec`.
- **`GetSystemMetrics`** (37 call sites, mostly DPI/screen-geometry queries) →
  `QScreen`/`QGuiApplication::primaryScreen()` geometry and `devicePixelRatio()`.
- **`RegisterPowerSettingNotification`/`WM_POWERBROADCAST`** → covered by the daemon's
  `/sys/class/power_supply` polling ([09](09-daemon-and-monitor.md)), not GUI-level.
- **`WM_DEVICECHANGE`** (HID hotplug detection, `alienfx-gui.cpp:675`) → udev
  monitoring (`libudev` `udev_monitor`) for hidraw add/remove events, surfaced to the
  GUI via the daemon's D-Bus signals rather than the GUI polling udev directly — keep
  device-list ownership in the daemon, consistent with the thin-client architecture
  above.
- **COM init** (`CoInitializeEx(COINIT_MULTITHREADED)`, `alienfx-gui.cpp:845`) — not
  needed at all on Linux; this was only for the WMI sensor calls being replaced in
  [05](05-alienfan-sdk-thermal.md)/[09](09-daemon-and-monitor.md).

## `alienfan-gui` and `alienfan-curve`

Same treatment, smaller scope: `alienfan-tools/alienfan-gui/alienfan-gui.cpp` (580
lines, `wWinMain` at `:75`, `CreateDialog(IDD_MAIN_VIEW)` at `:104`) and
`alienfan-tools/alienfan-curve/FanCurve.cpp` (481 lines, dialog-based fan curve editor)
should either become a dedicated "Fan" tab inside the unified Qt GUI, or ship as a
lightweight standalone Qt window reusing the same fan-curve widget — decide based on
whether the project wants one unified GUI or to preserve the Windows split between
"light tool" and "fan tool" binaries. This roadmap doesn't mandate either; note it as
an open decision for whoever picks up [10](10-gui-qt6.md)'s implementation, and record
the choice made in an update to this file once decided.

## Acceptance bar for this milestone

Feature parity is not required for an initial release — sequence sub-features
(ambient, haptics, hotkeys) as their own milestones per [17](17-milestones.md). The
acceptance bar for the *core* GUI milestone is: device/light management, color/effect
assignment via the visual grid, and profile management, all working end-to-end against
`alienfxd`, on both a native-Wayland and an X11 session.
