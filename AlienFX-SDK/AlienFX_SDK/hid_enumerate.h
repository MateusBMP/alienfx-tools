#pragma once
// Linux-only. The device-enumeration seam, mirroring exactly how hid_backend.h splits
// M2a from M2b (Doc/linux_roadmap/17-milestones.md's M2c section): AlienFX_SDK.cpp's
// Linux AlienFXProbeDevice/AlienFXInitialize call only this header, so alienfx::sdk
// never gains a hidapi dependency. Two providers:
//   - hid_enumerate_linux.cpp (M2c): the real implementation, over hidapi + a sysfs
//     report-descriptor read (see that file's header comment for why sysfs, not
//     hid_open_path, is read first).
//   - tests/support/stub_enumerate.cpp (M2c): a scripted stub returning nothing by
//     default, linked into every binary that pulls in alienfx::sdk but isn't
//     specifically testing enumeration -- see tests/CMakeLists.txt.
// Exactly one of the two is linked into any given binary.

#ifdef _WIN32
#error "hid_enumerate.h is Linux-only; the Windows build uses SetupAPI directly"
#endif

#include <optional>
#include <string>
#include <vector>

#include "hid_report_descriptor.h"
#include "win_compat.h"

namespace alienfx_hid {

// One HID node as seen by enumeration, with the caps AlienFXProbeDevice needs to
// decide a version already attached -- see hid_enumerate_linux.cpp's "dedupe by path"
// note for why this is one entry per hidraw node, not one per top-level collection.
struct HidNode {
	std::string path;                  // backend path, e.g. /dev/hidrawN
	uint16_t vid = 0, pid = 0;
	uint16_t usage = 0, usagePage = 0; // from hid_device_info -- NOT re-derived by
	                                    // this seam's own parsing; see
	                                    // hid_report_descriptor.h's file comment.
	std::string description;           // manufacturer + " " + product

	// Absent, not a parallel bool flag, when the report descriptor couldn't be read
	// or didn't parse -- a HidNode with no caps simply never matches the version
	// switch, the same as Windows' HidP_GetCaps failing outright.
	std::optional<HidCaps> caps;
};

// Enumerates HID nodes, pre-filtered to alienfx_hid::IsKnownVendor (hid_backend_linux.h)
// and further to vidFilter/pidFilter when non-zero (0 = wildcard, matching
// AlienFXInitialize's existing vid/pid semantics). Does not open any device. Returns
// false only on an enumeration-level failure (e.g. hid_init() itself failing) --
// finding zero matching nodes is success with an empty *out, not a failure.
bool EnumerateNodes(std::vector<HidNode>* out, uint16_t vidFilter, uint16_t pidFilter);

// Opens the node's backend path and registers it with alienfx_hid::RegisterDevice
// (hid_backend_linux.h) before returning, so the vendor-allowlist gate on every write
// path has a VID to check without a per-write hid_get_device_info() query. Returns
// nullptr on failure (including EACCES on a root-only node -- see
// hid_enumerate_linux.cpp for the diagnostic this emits in that case). HANDLE has no
// INVALID_HANDLE_VALUE-equivalent sentinel on this platform
// (Common/win_compat.h only defines HANDLE itself) -- nullptr is the only failure value.
HANDLE OpenNode(const HidNode& node);

} // namespace alienfx_hid
