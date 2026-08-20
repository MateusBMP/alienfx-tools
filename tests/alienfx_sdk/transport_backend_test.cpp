// M2b's fake-hidapi tier (Doc/linux_roadmap/16-testing-and-validation.md): replays
// tests/support/packet_matrix.h's shared version x operation matrix through
// AlienFX-SDK/AlienFX_SDK/hid_backend_linux.cpp -- the REAL backend M2b ships --
// linked against tests/support/fake_hidapi.cpp instead of a real hidapi
// implementation. Asserts byte-identical output against the SAME golden files
// tests/alienfx_sdk/packet_builder_test.cpp (M2a) checks against fake_hid.h,
// proving hid_backend_linux.cpp's Windows-call -> hidapi-call mapping is correct
// without hardware, root, or a real hidraw node -- see
// Doc/linux_roadmap/17-milestones.md's M2b section for why that split exists.
//
// Also covers the parts specific to this backend that packet_builder_test can't:
// the vendor allowlist gate, the hid_get_device_info() fallback when a handle
// wasn't registered, ReadFile's timeout-is-not-an-error mapping (defect 2), and
// that GetDeviceStatus's Get* calls now request their version's report ID
// (defect 1).

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "AlienFX_SDK.h"
#include "fake_hidapi.h"
#include "golden_vector.h"
#include "hid_backend.h"
#include "hid_backend_linux.h"
#include "packet_matrix.h"

#ifndef ALIENFX_GOLDEN_DIR
#error "ALIENFX_GOLDEN_DIR must be defined by tests/CMakeLists.txt"
#endif

using ::testing::ElementsAreArray;

namespace {

HANDLE const kTestHandle = reinterpret_cast<HANDLE>(0x1);

// Real vendor per API version, per AlienFXProbeDevice's detection switch
// (AlienFX_SDK.cpp:191-236, restated in Doc/linux_roadmap/04-alienfx-sdk-hid.md's
// "API version enum and detection" table). packet_matrix.h's PacketCase doesn't
// carry this -- M2a's fake never needed a vendor -- so this test derives it
// itself, the one piece of setup that's specific to exercising the allowlist
// gate.
uint16_t VendorForVersion(int version) {
	switch (version) {
	case AlienFX_SDK::API_V2:
	case AlienFX_SDK::API_V3:
	case AlienFX_SDK::API_V4:
	case AlienFX_SDK::API_V6:
		return 0x187c; // Alienware
	case AlienFX_SDK::API_V5:
		return 0x0d62; // Darfon
	case AlienFX_SDK::API_V7:
		return 0x0461; // Primax
	case AlienFX_SDK::API_V8:
		return 0x04f2; // Chicony
	default:
		return 0;
	}
}

void ResetGlobalState() {
	alienfx_hid::SetAllowAnyVendor(false);
	alienfx_hid::SetDryRun(false);
	alienfx_hid::SetDryRunSink(nullptr);
	alienfx_hid::ForgetDevice(kTestHandle);
	alienfx_test::GetFakeHidapi().Reset();
}

class TransportBackendTest : public ::testing::TestWithParam<alienfx_test::PacketCase> {
protected:
	void SetUp() override {
		ResetGlobalState();
		alienfx_hid::RegisterDevice(kTestHandle, VendorForVersion(GetParam().apiVersion), GetParam().pid);
	}
};

TEST_P(TransportBackendTest, MatchesGoldenVector) {
	const auto& c = GetParam();

	AlienFX_SDK::Functions f;
	f.version = c.apiVersion;
#ifdef ALIENFX_TESTING
	f.TestSetDeviceState(kTestHandle, c.hidReportLength, VendorForVersion(c.apiVersion), c.pid);
#endif
	c.run(f);

	const std::string path = std::string(ALIENFX_GOLDEN_DIR) + "/" + c.name + ".txt";
	const std::vector<alienfx_test::TransportEvent> expected = alienfx_test::ReadGoldenFile(path);
	EXPECT_THAT(alienfx_test::GetFakeHidapi().Log(), ElementsAreArray(expected));
}

std::string NameFromParam(const ::testing::TestParamInfo<alienfx_test::PacketCase>& info) {
	return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(AllCases, TransportBackendTest,
                          ::testing::ValuesIn(alienfx_test::AllPacketCases()),
                          NameFromParam);

// --- Cases specific to this backend, not covered by the golden-vector replay. --

class TransportBackendUnitTest : public ::testing::Test {
protected:
	void SetUp() override { ResetGlobalState(); }
	void TearDown() override { ResetGlobalState(); }
};

TEST_F(TransportBackendUnitTest, RefusesNonAllowlistedVendor) {
	alienfx_hid::RegisterDevice(kTestHandle, 0x1234, 0); // not one of the 5 known vendors
	uint8_t buffer[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
	EXPECT_FALSE(HidD_SetOutputReport(kTestHandle, buffer, sizeof(buffer)));
	EXPECT_FALSE(HidD_SetFeature(kTestHandle, buffer, sizeof(buffer)));
	DWORD written = 0;
	EXPECT_FALSE(WriteFile(kTestHandle, buffer, sizeof(buffer), &written, nullptr));
	EXPECT_TRUE(alienfx_test::GetFakeHidapi().Log().empty())
		<< "a refused write must not reach hidapi at all";
}

TEST_F(TransportBackendUnitTest, AllowAnyVendorOverrideBypassesGate) {
	alienfx_hid::RegisterDevice(kTestHandle, 0x1234, 0);
	alienfx_hid::SetAllowAnyVendor(true);
	uint8_t buffer[9] = {0};
	EXPECT_TRUE(HidD_SetOutputReport(kTestHandle, buffer, sizeof(buffer)));
	EXPECT_EQ(alienfx_test::GetFakeHidapi().Log().size(), 1u);
}

TEST_F(TransportBackendUnitTest, FallsBackToHidGetDeviceInfoWhenUnregistered) {
	// No alienfx_hid::RegisterDevice call -- the gate must fall back to
	// hid_get_device_info(), which fake_hidapi.h's SetDeviceInfo() backs.
	alienfx_test::GetFakeHidapi().SetDeviceInfo(
		reinterpret_cast<hid_device*>(kTestHandle), 0x187c, 0x0550);
	uint8_t buffer[9] = {0};
	EXPECT_TRUE(HidD_SetOutputReport(kTestHandle, buffer, sizeof(buffer)));
}

TEST_F(TransportBackendUnitTest, ReadFileTimeoutIsNotAnError) {
	// Defect 2: hid_read_timeout() returns 0 on timeout / -1 on error, distinct
	// from Windows' ReadFile, which returns TRUE with 0 bytes on timeout. The
	// fake always "succeeds" with a full-length response (it has no timeout
	// concept), so this asserts the wrapper's TRUE/byte-count contract that V7's
	// mandatory read-after-write depends on, not an actual timeout.
	alienfx_hid::RegisterDevice(kTestHandle, 0x187c, 0);
	uint8_t buffer[9] = {0};
	DWORD readBytes = 0xdeadbeef;
	EXPECT_TRUE(ReadFile(kTestHandle, buffer, sizeof(buffer), &readBytes, nullptr));
	EXPECT_EQ(readBytes, sizeof(buffer));
}

TEST_F(TransportBackendUnitTest, GetDeviceStatusRequestsItsVersionReportId) {
	// Defect 1: AlienFX_SDK.cpp's GetDeviceStatus now sets buffer[0] to
	// reportIDList[version] before every Get* call (three additive
	// #ifndef _WIN32 lines) -- exercised end to end via the public
	// IsDeviceReady() (GetDeviceStatus itself is private), so this breaks if that
	// wiring regresses rather than re-deriving reportIDList's values here. V5's
	// report ID is 0xcc (alienfx-controls.h's reportIDList, index API_V5).
	alienfx_hid::RegisterDevice(kTestHandle, 0x0d62, 0);
	AlienFX_SDK::Functions f;
	f.version = AlienFX_SDK::API_V5;
#ifdef ALIENFX_TESTING
	f.TestSetDeviceState(kTestHandle, 64, 0x0d62, 0);
#endif
	f.IsDeviceReady();

	bool sawGetFeatureCall = false;
	for (const auto& event : alienfx_test::GetFakeHidapi().Log())
		sawGetFeatureCall |= (event.kind == alienfx_test::TransportKind::GetFeat);
	EXPECT_TRUE(sawGetFeatureCall) << "IsDeviceReady() should have issued a HidD_GetFeature call";
	EXPECT_EQ(alienfx_test::GetFakeHidapi().LastRequestedReportId(), 0xcc);
}

TEST_F(TransportBackendUnitTest, DryRunDecodesWithoutCallingHidapi) {
	alienfx_hid::RegisterDevice(kTestHandle, 0x187c, 0);
	std::ostringstream sink;
	alienfx_hid::SetDryRunSink(&sink);
	alienfx_hid::SetDryRun(true);

	uint8_t buffer[9] = {2, 0x03, 0x04, 0, 0, 0, 0, 0, 0};
	EXPECT_TRUE(HidD_SetOutputReport(kTestHandle, buffer, sizeof(buffer)));

	EXPECT_TRUE(alienfx_test::GetFakeHidapi().Log().empty())
		<< "dry run must never reach hidapi";
	EXPECT_THAT(sink.str(), ::testing::HasSubstr("HidD_SetOutputReport"));
	EXPECT_THAT(sink.str(), ::testing::HasSubstr("report_id=0x02"));
}

} // namespace
