#pragma once
// Linux-only. Dependency-free HID report-descriptor parser -- no hidapi, no libudev,
// no I/O. Split into its own target (alienfx::hid_descriptor) specifically so its test
// suite has zero hidapi in its dependency graph, the same structural-cannot-open
// property M2a/M2b's tests have; see AlienFX-SDK/AlienFX_SDK/CMakeLists.txt for why that
// split matters, one level up from the identical argument that already split
// alienfx_hid_linux off alienfx_sdk.
//
// This exists because Linux hidraw exposes a device's raw HID report descriptor
// (readable from /sys/class/hidraw/hidrawN/device/report_descriptor with no special
// privilege) but nothing in hidapi's public API replaces Windows' HidP_GetCaps --
// AlienFXProbeDevice's detection switch (AlienFX_SDK.cpp) needs exactly what
// HidP_GetCaps gave it: report byte lengths. This parser reconstructs those from the
// raw descriptor bytes.
//
// See Doc/linux_roadmap/local/test-machine.md's "Finding 1" and
// Doc/linux_roadmap/04-alienfx-sdk-hid.md's "API version enum and detection" for why
// the +1 below is not optional, and why it must be conditional on a report existing.

#ifdef _WIN32
#error "hid_report_descriptor.h is Linux-only; Windows uses <hidsdi.h>/HidP_GetCaps directly"
#endif

#include <cstddef>
#include <cstdint>

namespace alienfx_hid {

// Deliberately NOT AfxHidCaps: Afx* is upstream's prefix (Afx_device/Afx_action/
// Afx_light); every fork-added Linux SDK symbol lives in namespace alienfx_hid
// (see hid_backend_linux.h), and this type follows that convention.
struct HidCaps {
	// Windows HIDP_CAPS convention: 0 if the node has no report of this kind at all,
	// otherwise ceil(bits/8) + 1 for the leading report-ID byte (present on the wire
	// whether or not the descriptor declares a Report ID item -- Windows always
	// reserves that byte). NOT simply "parsed byte count + 1": the +1 must not apply
	// to a genuinely absent report, or the API_V5 detection condition
	// (`!length`, AlienFX_SDK.cpp) can never be true again.
	int outputReportByteLength = 0;
	int featureReportByteLength = 0;
};

// Parses a raw HID report descriptor (as read from a hidraw node's sysfs
// report_descriptor attribute, or returned by hid_get_report_descriptor()) into
// HidCaps. Aggregates Output/Feature report bits across the WHOLE descriptor (every
// top-level collection in it), not per top-level collection the way Windows'
// HidP_GetCaps does -- see doc 04's note next to Finding 2 for why that is a
// deliberate, accepted gap for this milestone rather than an oversight.
//
// Returns false (and leaves *out unspecified) if the descriptor is malformed --
// truncated items, items running past the buffer end, or a resulting report length
// that would not fit AlienFX_SDK.h's MAX_BUFFERSIZE (193) -- rather than clamping.
// `data` is treated as fully untrusted: this is the trust boundary Linux does not get
// for free from a preparsed-data API the way Windows' HidP_GetCaps callers do.
bool ParseHidReportDescriptor(const uint8_t* data, size_t size, HidCaps* out);

} // namespace alienfx_hid
