// M2c: the real implementation of hid_enumerate.h's seam, over hidapi + a direct
// sysfs report-descriptor read. hid_backend_linux.cpp (M2b) deliberately never opens
// or enumerates a device -- this file is where that starts, exactly as
// Doc/linux_roadmap/17-milestones.md's M2c section describes.
//
// Sysfs, not hid_open_path, is read first: /sys/class/hidraw/hidrawN/device/
// report_descriptor is world-readable (verified: -r--r--r-- on this fork's test
// machine, while /dev/hidrawN itself is root-only until M2d's udev rule exists), and
// hidapi's own hidraw backend already reads that exact file during enumeration for
// exactly this reason -- this is not a novel privilege model. Reading it first breaks
// what would otherwise be a circular dependency: M2c's own exit criterion
// ("187c:0550 resolves to API_V4") could not be demonstrated without root if
// detection required opening the device first, since the udev rule granting non-root
// access is M2d's, not M2c's.
//
// This file should have no interesting branches of its own -- every branch worth
// independently asserting (the parser, the version switch) lives on the other side of
// this seam, in hid_report_descriptor.cpp and AlienFX_SDK.cpp respectively, both with
// dedicated test coverage. This file is enumerate -> filter -> dedupe -> read sysfs ->
// parse -> open -> register, in a straight line -- if a conditional worth testing
// shows up here, it belongs on the other side of the seam instead. That is also why
// this file has no automated test of its own: no test binary links
// alienfx::hid_enum_linux (see tests/CMakeLists.txt's stub_enumerate comment).

#include "hid_enumerate.h"

#include "hid_backend_linux.h" // alienfx_hid::IsKnownVendor, RegisterDevice
#include "hid_report_descriptor.h"

#include <hidapi.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <string>

namespace alienfx_hid {

namespace {

std::once_flag g_hidInitOnce;
bool g_hidInitOk = false;

// No hid_exit(): a std::call_once initializer has no natural shutdown hook, and
// calling hid_exit() from a static destructor risks tearing down state a still-live
// hid_device* depends on. This leaks hidapi's internal udev context for the life of
// the process -- harmless for a short-lived CLI invocation; worth revisiting once
// M6's long-running daemon exists.
void EnsureHidInit() {
	std::call_once(g_hidInitOnce, [] { g_hidInitOk = hid_init() == 0; });
}

// Windows builds `description` by appending each wchar_t of HidD_GetManufacturerString/
// HidD_GetProductString's output narrowed to a single char (AlienFX_SDK.cpp) -- a
// lossy, effectively-Latin-1 truncation, not a UTF-8 conversion. Matched here
// deliberately: this string becomes a config/display key in later milestones, and
// producing a *different* string than Windows for the same device would be a
// platform-dependent behavior change, not an improvement.
void AppendNarrowed(std::string* out, const wchar_t* wide) {
	if (!wide)
		return;
	for (const wchar_t* p = wide; *p; ++p)
		out->push_back(static_cast<char>(*p));
}

// Strictly validates that `path` is exactly /dev/hidraw<digits> and returns the sysfs
// report_descriptor path for it, or an empty path if it isn't -- never interpolates a
// backend-supplied string into a filesystem path by any looser means (e.g. a prefix
// check or string concatenation without validating the whole filename).
std::filesystem::path SysfsDescriptorPathFor(const std::string& hidrawPath) {
	static const std::regex kHidrawName("hidraw[0-9]+");
	std::string filename = std::filesystem::path(hidrawPath).filename().string();
	if (!std::regex_match(filename, kHidrawName))
		return {};
	return std::filesystem::path("/sys/class/hidraw") / filename / "device" / "report_descriptor";
}

std::optional<HidCaps> ReadCapsFromSysfs(const std::string& hidrawPath) {
	std::filesystem::path descPath = SysfsDescriptorPathFor(hidrawPath);
	if (descPath.empty())
		return std::nullopt; // TODO(M2e/M3): libusb-backed hidapi has no sysfs node;
		                      // add a hid_get_report_descriptor fallback if a libusb
		                      // backend is ever added to this build. Not exercised by
		                      // this build graph today -- see hid_enumerate.h.

	std::ifstream f(descPath, std::ios::binary);
	if (!f)
		return std::nullopt;
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
	                            std::istreambuf_iterator<char>());
	HidCaps caps;
	if (!ParseHidReportDescriptor(bytes.data(), bytes.size(), &caps))
		return std::nullopt;
	return caps;
}

} // namespace

bool EnumerateNodes(std::vector<HidNode>* out, uint16_t vidFilter, uint16_t pidFilter) {
	out->clear();
	EnsureHidInit();
	if (!g_hidInitOk)
		return false;

	hid_device_info* list = hid_enumerate(vidFilter, pidFilter);
	// A null return is hidapi's "nothing matched or an error" signal for both cases;
	// hid_error(NULL) is the only diagnostic hidapi offers before any device is open,
	// and "nothing matched" is not itself a failure this function reports as one --
	// EnumerateNodes returning true with an empty *out is the normal "no candidate
	// devices present" outcome.
	if (!list)
		return true;

	for (hid_device_info* info = list; info; info = info->next) {
		if (!info->path || !IsKnownVendor(info->vendor_id))
			continue; // pre-filter to the five known AlienFX vendors before doing
			          // anything else -- behavior-preserving (AlienFX_SDK.cpp's
			          // version switch can't match any other vendor) and strictly
			          // safer: this process never touches unrelated HID hardware.

		std::string path(info->path);

		// Dedupe by path: hid_enumerate() yields one hid_device_info per top-level
		// collection, so a composite device (confirmed on this fork's test machine:
		// the Darfon keyboard) appears more than once with an identical path. Keep
		// the first entry's usage/usagePage -- later collections of the same node
		// don't change which hidraw node AlienFXProbeDevice would open.
		bool alreadySeen = false;
		for (const HidNode& existing : *out)
			if (existing.path == path) { alreadySeen = true; break; }
		if (alreadySeen)
			continue;

		HidNode node;
		node.path = path;
		node.vid = info->vendor_id;
		node.pid = info->product_id;
		node.usage = info->usage;
		node.usagePage = info->usage_page;
		AppendNarrowed(&node.description, info->manufacturer_string);
		node.description += " ";
		AppendNarrowed(&node.description, info->product_string);
		node.caps = ReadCapsFromSysfs(path);
		out->push_back(std::move(node));
	}

	hid_free_enumeration(list);
	return true;
}

HANDLE OpenNode(const HidNode& node) {
	hid_device* dev = hid_open_path(node.path.c_str());
	if (!dev) {
		if (node.caps.has_value()) {
			// A node whose descriptor resolved to a known API version but that
			// still couldn't be opened -- almost certainly EACCES on a root-only
			// hidraw node (this fork's test machine: crw------- root:root, no udev
			// rule yet). Killing this exact silent-failure mode is half of what
			// this milestone exists to do -- see Finding 1
			// (Doc/linux_roadmap/local/test-machine.md). To stderr, not stdout:
			// hid_backend_linux.cpp's Sink()/SetDryRunSink writes to stdout by
			// default, and a second independent stdout writer here would both
			// pollute alienfx-cli's future output (M3) and be un-redirectable by
			// that existing hook.
			// %s here takes a NARROW char* even inside a wide (fwprintf) format
			// string -- only an 'l'-modified %ls conversion (used below, for
			// hid_error()'s wchar_t* result) reads a wide string. Passing
			// node.path (already narrow) as %s directly is correct; widening it
			// first would make fwprintf misinterpret the wchar_t buffer's raw
			// bytes as a narrow string instead (confirmed on real hardware: it
			// printed a bare "/" for "/dev/hidraw3" before this was fixed).
			const wchar_t* err = hid_error(nullptr);
			std::fwprintf(stderr,
				L"alienfx: found a supported device at %s but could not open it"
				L" (%ls) -- install the udev rule described in"
				L" Doc/linux_roadmap/15-packaging-and-permissions.md, or run as root\n",
				node.path.c_str(),
				err ? err : L"unknown error");
		}
		return nullptr;
	}
	RegisterDevice(dev, node.vid, node.pid);
	return dev;
}

} // namespace alienfx_hid
