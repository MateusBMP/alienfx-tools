# Ambient Lighting: Screen Capture

## Current implementation

`alienfx-gui/DXGIManager.hpp`/`.cpp` (130 + 379 lines) is vendored third-party MIT code
(Johan Johansson, 2015) implementing DXGI desktop duplication: `<atlbase.h>` (ATL
`CComPtr`/`CComQIPtr` — MSVC-only), `<DXGI1_2.h>`, `<d3d11.h>`, `<Wincodec.h>`,
`#pragma comment(lib,"dxgi.lib")`/`"d3d11.lib"`. Core calls:
`CreateDXGIFactory1` → `D3D11CreateDevice` → `IDXGIOutput1::DuplicateOutput` →
`IDXGIOutputDuplication::AcquireNextFrame` → `ID3D11DeviceContext::CopyResource` →
`IDXGISurface1::Map(DXGI_MAP_READ)`. Driven by `alienfx-gui/CaptureHelper.cpp` (237
lines), which owns a `DXGIManager` instance and polls it on a 100ms `ThreadHelper` tick
(see [03](03-platform-abstraction.md) for the `ThreadHelper` replacement).

There is no salvageable code here for Linux — this entire capture layer (~510 lines,
plus the ATL dependency) is a full rewrite. What *does* carry over unchanged: the
100ms polling cadence, and the ambient config keys (`Ambient-Shift`, `Ambient-Calc`,
`Ambient-Mode`, `Ambient-Grid` in `alienfx-gui/ConfigHandler.cpp` — see
[06](06-configuration-storage.md)) and their semantics (color-averaging strategy,
per-grid-cell sampling for keyboard ambient effects).

## Linux replacement, by display server

- **Wayland (primary target)**: PipeWire + `xdg-desktop-portal` ScreenCast API. The
  application requests a screen/window capture session through the portal (D-Bus),
  which — after a **one-time user consent dialog per compositor session** — hands back
  a PipeWire stream of frames. This is the standard, compositor-agnostic way to capture
  screen content under Wayland; there is no lower-level API that works across
  compositors, by design (the same sandboxing philosophy that also blocks
  foreground-window detection, see [09](09-daemon-and-monitor.md)).
- **X11 fallback**: `XShm`/XComposite (via Xlib or XCB) for direct, no-consent-prompt
  capture — this is the closer analogue to DXGI's zero-friction desktop duplication
  and should be preferred when running under a pure X11 session (not XWayland-only).
- **Headless/DRM (stretch)**: DRM/KMS framebuffer capture for setups with no compositor
  at all (rare for a desktop lighting tool, low priority).

## UX consequence: the Wayland consent prompt breaks "silent background effect"

DXGI desktop duplication on Windows is silent and instantaneous — the ambient effect
"just works" the moment it's enabled. The portal-based Wayland path requires an
explicit per-session user consent dialog, and depending on the portal implementation,
that consent may need to be **re-granted every session** (not persisted), which is a
meaningfully worse UX than the Windows tool has today. Document this limitation
directly in the GUI's ambient settings ([10](10-gui-qt6.md)) rather than trying to
suppress or auto-accept the prompt (which isn't possible without compositor
cooperation, and shouldn't be circumvented even if it were — it exists for user
privacy). Offer the X11 path as the "no prompt" option where available.

## Architecture placement

Capture should live in the daemon (`alienfxd`, [09](09-daemon-and-monitor.md)) rather
than the GUI process, for two reasons: (1) the ambient effect needs to keep running
when the GUI window is closed, matching the Windows tool's tray-resident behavior, and
(2) it avoids requesting portal/PipeWire permissions from a process that also wants a
visible window — a background capture session is a cleaner permission story. The 100ms
capture-and-average loop feeds directly into the same light-update path the daemon
already uses for other effects.

## Color-averaging logic

The Windows code's actual pixel→color reduction logic (averaging/sampling strategy per
grid cell) lives downstream of `DXGIManager`'s raw frame buffer, in
`CaptureHelper.cpp`/`AmbientDialog.cpp` — that logic is display-API-independent and
should port with only minor changes once frames arrive as a raw RGBA buffer from either
the PipeWire or X11 path, regardless of source. Don't rewrite the averaging math as
part of this milestone; only the frame-acquisition layer changes.
