// M2a's golden-vector generator (Doc/linux_roadmap/16-testing-and-validation.md,
// "The recording fake transport"). Run by hand, NOT wired into ctest -- golden
// vectors are committed artifacts, and re-running this should reproduce them
// byte-for-byte (an empty `git diff`), which is exactly what
// packet_builder_test.cpp checks on every test run instead.
//
// Usage: gen_golden <output-dir>
//   e.g. build/dev/bin/gen_golden tests/golden/alienfx_sdk

#include <cstdio>
#include <string>

#include "AlienFX_SDK.h"
#include "fake_hid.h"
#include "golden_vector.h"
#include "packet_matrix.h"

namespace {

const char* ApiVersionName(int version) {
	switch (version) {
	case AlienFX_SDK::API_V2: return "v2";
	case AlienFX_SDK::API_V3: return "v3";
	case AlienFX_SDK::API_V4: return "v4";
	case AlienFX_SDK::API_V5: return "v5";
	case AlienFX_SDK::API_V6: return "v6";
	case AlienFX_SDK::API_V7: return "v7";
	case AlienFX_SDK::API_V8: return "v8";
	default: return "unknown";
	}
}

} // namespace

int main(int argc, char** argv) {
	if (argc != 2) {
		std::fprintf(stderr, "usage: %s <output-dir>\n", argv[0]);
		return 2;
	}
	const std::string outDir = argv[1];

	for (const auto& c : alienfx_test::AllPacketCases()) {
		AlienFX_SDK::Functions f;
		f.version = c.apiVersion;
#ifdef ALIENFX_TESTING
		f.TestSetDeviceState(reinterpret_cast<HANDLE>(0x1), c.hidReportLength, c.vid, c.pid);
#endif
		alienfx_test::GetFakeTransport().Reset();
		c.run(f);

		const std::string path = outDir + "/" + c.name + ".txt";
		alienfx_test::WriteGoldenFile(
			path,
			{
				"origin=source-derived api=" + std::string(ApiVersionName(c.apiVersion)) + " call=" + c.name,
				"NOT a hardware capture -- pins the current packet builder's output only. "
				"See Doc/linux_roadmap/16-testing-and-validation.md, \"source-derived\" tier.",
			},
			alienfx_test::GetFakeTransport().Log());
		std::printf("wrote %s (%zu events)\n", path.c_str(), alienfx_test::GetFakeTransport().Log().size());
	}
	return 0;
}
