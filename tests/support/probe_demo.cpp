// Hand-run demo (not wired into ctest, same convention as gen_golden.cpp/
// dry_run_demo.cpp) of the real enumeration + detection path -- links the real
// alienfx::hid_enum_linux + alienfx::hid_linux + hidapi::hidraw. Demonstrates
// Doc/linux_roadmap/17-milestones.md's M2c exit criterion end to end: for every
// enumerated candidate, prints its VID:PID, parsed report-descriptor caps, and
// resolved API version.
//
// Never sends anything to a device: a matched node is opened (the same thing
// AlienFXProbeDevice always does to confirm a match) and immediately closed again
// when `f` goes out of scope, but PrepareAndSend/Reset/SetColor/etc. are never
// called. Runs as a normal user -- detection itself reads sysfs, not the hidraw
// node -- but *opening* a matched node still needs the udev rule M2d adds; run with
// sudo to see "(opened OK)", or read the caps line plus hid_enumerate_linux.cpp's
// own stderr diagnostic (which distinguishes "found a supported device but could
// not open it" from "no match") to confirm detection without root.
//
// Usage: probe_demo

#include "AlienFX_SDK.h"
#include "hid_enumerate.h"

#include <cstdio>
#include <vector>

int main() {
	std::vector<alienfx_hid::HidNode> nodes;
	if (!alienfx_hid::EnumerateNodes(&nodes, 0, 0)) {
		std::fprintf(stderr, "probe_demo: enumeration failed\n");
		return 1;
	}
	if (nodes.empty()) {
		std::printf("no known-vendor HID candidates found\n");
		return 0;
	}

	for (auto& node : nodes) {
		std::printf("%-16s %04x:%04x  usage=0x%04x/0x%04x  ", node.path.c_str(),
		            node.vid, node.pid, node.usagePage, node.usage);
		if (!node.caps.has_value()) {
			std::printf("descriptor unreadable or unparsed\n");
			continue;
		}
		std::printf("out=%-3d feat=%-3d  ", node.caps->outputReportByteLength,
		            node.caps->featureReportByteLength);

		AlienFX_SDK::Functions f;
		if (f.AlienFXProbeDevice(nullptr, &node))
			std::printf("-> API_V%d (opened OK)\n", f.version);
		else
			std::printf("-> no match, or a matching device could not be opened"
			            " (see stderr)\n");
	}
	return 0;
}
