#pragma once
// The shared version x operation matrix gen_golden and packet_builder_test both
// replay, so the two are provably driving identical calls -- see
// Doc/linux_roadmap/16-testing-and-validation.md, "source-derived" tier.

#include <functional>
#include <string>
#include <vector>

#include "AlienFX_SDK.h"

namespace alienfx_test {

struct PacketCase {
	std::string name;         // golden file stem: tests/golden/alienfx_sdk/<name>.txt
	int apiVersion;           // AlienFX_SDK::API_V2 .. API_V8
	int hidReportLength;      // 9/12/34/64/65/65/65, matches AlienFX_SDK.h's enum comments
	unsigned short vid = 0;
	unsigned short pid = 0;   // 0x551 for V4 cases -- bypasses WaitForBusy, see fake_hid.h
	std::function<void(AlienFX_SDK::Functions&)> run;
};

const std::vector<PacketCase>& AllPacketCases();

} // namespace alienfx_test
