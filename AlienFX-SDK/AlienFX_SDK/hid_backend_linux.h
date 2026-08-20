#pragma once
// Control surface for hid_backend_linux.cpp (M2b), the real hidapi-backed
// implementation of hid_backend.h's transport seam. Doc/linux_roadmap/
// 04-alienfx-sdk-hid.md's "hidraw / hidapi mapping" documents the call mapping;
// Doc/linux_roadmap/15-packaging-and-permissions.md documents the vendor
// allowlist enforced here.
//
// hid_backend.h's free functions take an opaque HANDLE (== hid_device* here, see
// AlienFX_SDK.h:124) with no VID/PID attached -- the allowlist gate needs to know
// a handle's VID before it can decide whether to let a write through, so whoever
// opens the device must call RegisterDevice() once, immediately after hid_open*()
// succeeds. M2b never opens a device itself (that's M2c's), so today only tests
// call this directly; M2c's real enumeration code becomes the real caller.

#ifdef _WIN32
#error "hid_backend_linux.h is Linux-only"
#endif

#include <cstdint>
#include <iosfwd>

#include "win_compat.h"

namespace alienfx_hid {

// --- Device registry, see the file comment above. ---------------------------
void RegisterDevice(HANDLE handle, uint16_t vid, uint16_t pid);
void ForgetDevice(HANDLE handle);

// --- Dry run: decode-and-print instead of sending. Seeded from ALIENFX_DRY_RUN
// (checked once, at first use of any wrapper) and overridable at runtime, e.g.
// for tests/support/dry_run_demo.cpp. ---------------------------------------
void SetDryRun(bool enabled);
bool IsDryRun();
void SetDryRunSink(std::ostream* sink); // nullptr restores std::cout

// --- Vendor allowlist gate. Every write path (SetOutputReport/SetFeature/
// WriteFile's interrupt write) refuses a handle whose registered VID is not one
// of the five known AlienFX vendors unless this is set -- seeded from
// ALIENFX_ALLOW_ANY_VENDOR. ---------------------------------------------------
void SetAllowAnyVendor(bool enabled);
bool IsAllowAnyVendor();

// --- HidD_SetOutputReport transport choice. Windows' SET_OUTPUT_REPORT is a
// control-endpoint Set_Report transfer; hidapi's matching call
// (hid_send_output_report) only exists from 0.15.0, so a build against an older
// hidapi falls back to hid_write (an interrupt transfer -- different on the
// wire). Seeded from ALIENFX_HID_OUTPUT_MODE=report|write so M2d can settle this
// against real V2/V3/V4 hardware without a rebuild. -------------------------
enum class OutputMode { Report, Write };
void SetOutputMode(OutputMode mode);
OutputMode GetOutputMode();

// --- Sleep observability. Sleep() (below) has no hidapi equivalent to fake, so
// it always really sleeps -- this hook exists purely so
// tests/support/fake_hidapi.cpp can additionally *log* each call (matching
// tests/support/fake_hid.h's Sleep event, so a golden vector's `> sleep <ms>`
// lines compare identically whichever fake backs the test) without changing
// production behavior, which never installs one. -----------------------------
using SleepObserver = void (*)(unsigned milliseconds);
void SetSleepObserver(SleepObserver observer); // nullptr removes it (the default)

} // namespace alienfx_hid
