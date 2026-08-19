# Global Input: Hotkeys and Key-Press Lighting

## Current implementation

Two distinct Windows input mechanisms are in play, and they map to different Linux
solutions:

### Low-level keyboard/mouse hooks (per-key lighting reaction)

`SetWindowsHookEx(WH_KEYBOARD_LL, ...)` at `KeyPressDialog.cpp:41` (with
`UnhookWindowsHookEx` at `:44`, `KBDLLHOOKSTRUCT`, `MapVirtualKey(MAPVK_VK_TO_VSC_EX)`
at `:9`, `GetKeyNameText` at `:12,24`); also `GridHelper.cpp:161`
(`WH_KEYBOARD_LL` + `GetAsyncKeyState` at `:55`); also `EventHandler.cpp:273,282,283`
(`WH_KEYBOARD_LL` ×2 and `WH_MOUSE_LL`, `CallNextHookEx` at `:343,381`). These drive
per-key-press lighting reactions (e.g. keyboards that flash a key when it's pressed)
and the key-binding capture UI (`KeyPressDialog` lets a user press a key to select it
for a hotkey).

### Global application hotkeys

`RegisterHotKey` × ~10 in `alienfx-gui.cpp:94-111` — F17/F18, Ctrl+Shift+F9..F12,
Ctrl+Shift+0..9, Ctrl+Alt+0..9 — bound to profile-switching and quick actions,
independent of which window has focus.

## Linux replacement, by mechanism

### Key-press-reactive lighting (`WH_KEYBOARD_LL`, `GridHelper.cpp`)

Needs **raw keyboard event access independent of window focus**, which is the same
class of requirement as low-level hooks on Windows. Options:

- **`/dev/input/event*` via `libevdev`** — reads raw kernel input events directly,
  works regardless of display server (X11 or Wayland), but requires the running user
  to be in the `input` group (or a udev rule granting access — see
  [15](15-packaging-and-permissions.md)) since `/dev/input/event*` isn't
  world-readable by default. This is the most broadly compatible option and should be
  the primary implementation.
- Per-compositor alternatives exist on Wayland for narrower purposes but don't cover
  "react to every keypress across the whole system" the way `libevdev` does — don't
  pursue them for this specific feature.

**Be explicit in the GUI/docs that this is the single feature most at risk under a
sandboxed or hardened Wayland setup** (e.g. under Flatpak confinement — see
[15](15-packaging-and-permissions.md)'s note on why Flatpak is a poor packaging fit
here) — `/dev/input` access can't be virtualized the way portal-based screen capture
can. If `libevdev` access is unavailable at runtime, the feature should disable itself
with a clear message, not silently do nothing.

`GetKeyNameText`/`MapVirtualKey` (for displaying human-readable key names in the
binding UI) → evdev keycode tables (`linux/input-event-codes.h` `KEY_*` constants) plus
a keycode→display-name lookup, which `libevdev` or a small embedded table can provide.

### Global hotkeys (`RegisterHotKey`)

- **X11**: `XGrabKey` per hotkey combination — direct equivalent of `RegisterHotKey`,
  works today, no consent prompts.
- **Wayland**: `org.freedesktop.portal.GlobalShortcuts` — the portal-based
  equivalent, requiring the same per-session/compositor consent-and-registration flow
  as other portal APIs (see [11](11-ambient-capture.md)'s note on portal UX
  friction). Not all compositors implement this portal yet at the time of writing —
  treat as best-effort and degrade gracefully (hotkey binding UI shows "unavailable on
  this compositor" rather than failing silently) where unsupported.

Since key-press-reactive lighting already requires `libevdev`/`/dev/input` access, an
alternative worth considering at implementation time: bind global hotkeys through the
same evdev event stream rather than through two separate mechanisms (`XGrabKey` +
portal). This trades portal-based Wayland compositor consent UX for a single unified
input path with the same permission requirement the key-reactive-lighting feature
already has. Decide based on how [15](15-packaging-and-permissions.md) resolves the
`input`-group permission question — if users already need `input` group membership for
key-reactive lighting, reusing that path for hotkeys avoids adding a second
permission model.

## Architecture placement

Both mechanisms are naturally daemon-owned (`alienfxd`,
[09](09-daemon-and-monitor.md)) rather than GUI-owned, since they need to function
whether or not the GUI window is open, exactly like the Windows tools' tray-resident
hotkey handling. The GUI's binding-capture dialogs
(`KeyPressDialog.cpp`/`EventDialog.cpp`, see [10](10-gui-qt6.md)) become thin UI that
asks the daemon to enter "capture next keypress" mode rather than opening their own
hook.

## Priority note

Sequence after [10](10-gui-qt6.md)'s core GUI, alongside [11](11-ambient-capture.md)
and [12](12-audio-haptics.md) — none of these three block core light/fan control, and
all three carry the heaviest Wayland-specific caveats in this roadmap.
