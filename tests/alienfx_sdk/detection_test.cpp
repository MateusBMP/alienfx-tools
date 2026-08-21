// M2c: tests for AlienFX_SDK.cpp's Linux AlienFXProbeDevice/AlienFXInitialize
// (the #else arm rewritten from M2a/M2b's no-op stubs). Drives them through
// hand-built alienfx_hid::HidNode values and tests/support/stub_enumerate.h's
// scripting API -- no real hidapi, no real device, anywhere in this binary (see
// tests/CMakeLists.txt's detection_test target).
//
// Every test asserts the fake transport's call log stays empty: detection must
// never reach PrepareAndSend. That is this milestone's core safety property, so it
// is checked in every case here, not just its own dedicated one.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "AlienFX_SDK.h"
#include "fake_hid.h"
#include "hid_report_descriptor.h"
#include "stub_enumerate.h"

using AlienFX_SDK::Functions;

namespace {

void ResetAll() {
	alienfx_test::GetFakeTransport().Reset();
	alienfx_test::ResetEnumStub();
}

void ExpectNoTransportActivity() {
	EXPECT_TRUE(alienfx_test::GetFakeTransport().Log().empty())
		<< "detection must never reach PrepareAndSend";
}

alienfx_hid::HidNode MakeNode(std::string path, uint16_t vid, uint16_t pid,
                               uint16_t usage, int outputLen, int featureLen = 0) {
	alienfx_hid::HidNode node;
	node.path = std::move(path);
	node.vid = vid;
	node.pid = pid;
	node.usage = usage;
	node.description = "Test Device";
	node.caps = alienfx_hid::HidCaps{outputLen, featureLen};
	return node;
}

} // namespace

// --- The named exit criterion: the full parser + detection pipeline ---------------

TEST(Detection, Vid187cReportCount33ResolvesToApiV4) {
	// Doc/linux_roadmap/17-milestones.md's M2c exit criterion, verbatim: "a
	// regression test asserts VID 0x187c + parsed Report Count 33 -> API_V4."
	// Parses the exact 25 bytes from this fork's test machine
	// (report_descriptor_test.cpp's TestMachineV4DescriptorResolvesTo34 proves the
	// parser alone normalizes 33 -> 34; this test proves the whole pipeline, parser
	// through AlienFXProbeDevice's version switch, resolves to API_V4).
	ResetAll();
	alienfx_test::SetOpenNodeSucceeds(true);

	std::vector<uint8_t> descriptor = {
		0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x15, 0x00, 0x26, 0xff, 0x00,
		0x75, 0x08, 0x95, 0x21, 0x09, 0x01, 0x81, 0x00, 0x09, 0x01, 0x91, 0x00, 0xc0,
	};
	alienfx_hid::HidCaps caps;
	ASSERT_TRUE(alienfx_hid::ParseHidReportDescriptor(descriptor.data(), descriptor.size(), &caps));
	ASSERT_EQ(caps.outputReportByteLength, 34); // pinned again, at the seam boundary

	alienfx_hid::HidNode node;
	node.path = "/dev/hidraw3";
	node.vid = 0x187c;
	node.pid = 0x0550;
	node.caps = caps;

	Functions f;
	EXPECT_TRUE(f.AlienFXProbeDevice(nullptr, &node));
	EXPECT_EQ(f.version, AlienFX_SDK::API_V4);
	EXPECT_EQ(f.vid, 0x187c);
	EXPECT_EQ(f.pid, 0x0550);
	ExpectNoTransportActivity();
}

// --- The version switch, table-driven -----------------------------------------

struct DetectionCase {
	std::string name;
	uint16_t vid, pid, usage;
	int outputLen;
	int expectedVersion;
};

class DetectionVersionSwitch : public ::testing::TestWithParam<DetectionCase> {};

TEST_P(DetectionVersionSwitch, ResolvesToExpectedVersion) {
	const auto& c = GetParam();
	ResetAll();
	alienfx_test::SetOpenNodeSucceeds(true); // a match, if any, must be able to open

	alienfx_hid::HidNode node = MakeNode("/dev/hidraw0", c.vid, c.pid, c.usage, c.outputLen);
	Functions f;
	bool matched = f.AlienFXProbeDevice(nullptr, &node);

	EXPECT_EQ(matched, c.expectedVersion != AlienFX_SDK::API_UNKNOWN);
	EXPECT_EQ(f.version, c.expectedVersion);
	ExpectNoTransportActivity();
}

std::string NameFromParam(const ::testing::TestParamInfo<DetectionCase>& info) {
	return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(AllVersions, DetectionVersionSwitch, ::testing::ValuesIn(
	std::vector<DetectionCase>{
		{"V2_9bytes",                0x187c, 0x1111, 0, 9,  AlienFX_SDK::API_V2},
		{"V3_12bytes",               0x187c, 0x1111, 0, 12, AlienFX_SDK::API_V3},
		{"V4_34bytes",               0x187c, 0x1111, 0, 34, AlienFX_SDK::API_V4},
		{"V6_65bytes_alienware",     0x187c, 0x1111, 0, 65, AlienFX_SDK::API_V6},
		{"V6_65bytes_microchip",     0x0424, 0x0001, 0, 65, AlienFX_SDK::API_V6},
		{"V7_65bytes_primax",        0x0461, 0x0001, 0, 65, AlienFX_SDK::API_V7},
		{"V8_65bytes_chicony",       0x04f2, 0x0001, 0, 65, AlienFX_SDK::API_V8},
		{"UnknownVendor_65bytes",    0x1234, 0x0001, 0, 65, AlienFX_SDK::API_UNKNOWN},
		{"Alienware_UnmatchedLength",0x187c, 0x1111, 0, 10, AlienFX_SDK::API_UNKNOWN},
	}), NameFromParam);

// --- The hub exclusion: one arm with a real "don't touch this device" consequence -

TEST(Detection, MicrochipHubExcludedFromV6) {
	ResetAll();
	alienfx_test::SetOpenNodeSucceeds(true);
	alienfx_hid::HidNode node = MakeNode("/dev/hidraw0", 0x0424, 0x274c, 0, 65);
	Functions f;
	EXPECT_FALSE(f.AlienFXProbeDevice(nullptr, &node));
	EXPECT_EQ(f.version, AlienFX_SDK::API_UNKNOWN);
	ExpectNoTransportActivity();
}

// --- Finding 2's starting state: must stay undetected until M2e ------------------

TEST(Detection, DarfonCompositeNodeDoesNotResolveToV5) {
	// A whole-node aggregate output length that's non-zero (Finding 2's shape: a
	// sibling boot-keyboard collection's LED report) must NOT accidentally satisfy
	// the V5 `!length` condition just because usage == 0xcc. Guards against
	// "fixing" Finding 2 by accident and driving a device
	// alienfx-gui/Mappings/devices.csv:130 marks Unused.
	ResetAll();
	alienfx_test::SetOpenNodeSucceeds(true);
	alienfx_hid::HidNode node = MakeNode("/dev/hidraw4", 0x0d62, 0x3740, 0xcc,
	                                      /*outputLen=*/2, /*featureLen=*/8);
	Functions f;
	EXPECT_FALSE(f.AlienFXProbeDevice(nullptr, &node));
	EXPECT_EQ(f.version, AlienFX_SDK::API_UNKNOWN);
	ExpectNoTransportActivity();
}

TEST(Detection, DarfonNodeWithGenuinelyZeroOutputResolvesToV5) {
	// The reachable-in-principle counterpart to the case above: when the whole-node
	// output length genuinely is zero (no sibling Output report at all), V5 must
	// still be detectable -- proving the conditional +1 fix
	// (hid_report_descriptor.h) doesn't collaterally disable this path.
	ResetAll();
	alienfx_test::SetOpenNodeSucceeds(true);
	alienfx_hid::HidNode node = MakeNode("/dev/hidraw4", 0x0d62, 0x3740, 0xcc,
	                                      /*outputLen=*/0, /*featureLen=*/8);
	Functions f;
	EXPECT_TRUE(f.AlienFXProbeDevice(nullptr, &node));
	EXPECT_EQ(f.version, AlienFX_SDK::API_V5);
	ExpectNoTransportActivity();
}

// --- Ordering hazard: a match whose OpenNode fails must not half-set state -------

TEST(Detection, FailedOpenRollsVersionBackToUnknown) {
	// SetOpenNodeSucceeds defaults to false (ResetAll's ResetEnumStub) -- so a node
	// whose caps match a known version still fails to open here, exactly like
	// EACCES on a root-only hidraw node before M2d's udev rule exists.
	ResetAll();
	alienfx_hid::HidNode node = MakeNode("/dev/hidraw3", 0x187c, 0x0550, 0, 34);
	Functions f;
	EXPECT_FALSE(f.AlienFXProbeDevice(nullptr, &node));
	EXPECT_EQ(f.version, AlienFX_SDK::API_UNKNOWN);
	// devHandle itself is private and has no public accessor; version staying
	// API_UNKNOWN is the behaviorally meaningful invariant (it's what keeps
	// ~Functions from calling CloseHandle on a handle that was never opened -- see
	// AlienFX_SDK.cpp's comment at this rollback). PrepareAndSend safely no-ops
	// whenever devHandle is null (`if (this && devHandle)`, AlienFX_SDK.cpp), so
	// calling it here and confirming no transport activity is an additional,
	// crash-safe check that nothing was left in a half-open state.
	EXPECT_FALSE(f.PrepareAndSend(nullptr));
	ExpectNoTransportActivity();
}

// --- AlienFXInitialize: enumeration + first-match-wins ----------------------------

TEST(Detection, InitializeHonoursVidPidFilterArguments) {
	ResetAll();
	alienfx_test::SetOpenNodeSucceeds(true);
	alienfx_test::SetEnumNodes({
		MakeNode("/dev/hidraw0", 0x0461, 0x0001, 0, 65),  // V7 -- must be filtered out
		MakeNode("/dev/hidraw1", 0x187c, 0x0550, 0, 34),  // V4 -- the one we want
	});
	Functions f;
	EXPECT_TRUE(f.AlienFXInitialize(0x187c, 0));
	EXPECT_EQ(f.version, AlienFX_SDK::API_V4);
	EXPECT_EQ(f.vid, 0x187c);
	ExpectNoTransportActivity();
}

TEST(Detection, InitializeSkipsUnopenableNodeAndContinues) {
	ResetAll();
	alienfx_test::SetOpenNodeSucceeds(true);
	alienfx_hid::HidNode n1 = MakeNode("/dev/hidraw0", 0x187c, 0x0551, 0, 34);
	alienfx_hid::HidNode n2 = MakeNode("/dev/hidraw1", 0x187c, 0x0552, 0, 34);
	alienfx_test::SetEnumNodes({n1, n2});
	alienfx_test::SetUnopenablePaths({n1.path});

	Functions f;
	EXPECT_TRUE(f.AlienFXInitialize(0, 0));
	EXPECT_EQ(f.version, AlienFX_SDK::API_V4);
	EXPECT_EQ(f.pid, 0x0552); // proves it moved past n1's failed open to n2
	ExpectNoTransportActivity();
}

TEST(Detection, InitializeRecordsVidOnSuccessfulOpen) {
	// Standing in for "RegisterDevice called exactly once on success with the
	// correct VID": stub_enumerate.cpp's OpenNode records this itself, as a fake
	// analogous to the real alienfx_hid::RegisterDevice a real backend calls (see
	// hid_enumerate.h) -- this stub deliberately never calls the real one, so
	// linking it never pulls in hidapi (stub_enumerate.h's file comment).
	ResetAll();
	alienfx_test::SetOpenNodeSucceeds(true);
	alienfx_test::SetEnumNodes({MakeNode("/dev/hidraw0", 0x04f2, 0x1234, 0, 65)});

	Functions f;
	ASSERT_TRUE(f.AlienFXInitialize(0, 0));
	EXPECT_EQ(f.version, AlienFX_SDK::API_V8);
	EXPECT_EQ(alienfx_test::OpenNodeSuccessCount(), 1u);
	EXPECT_EQ(alienfx_test::LastRegisteredVid(), 0x04f2);
	EXPECT_EQ(alienfx_test::LastRegisteredPid(), 0x1234);
	ExpectNoTransportActivity();
}

TEST(Detection, InitializeReinitializationSucceedsAfterPriorOpen) {
	// fake_hid.cpp's CloseHandle deliberately never logs its calls (see that
	// file's own comment: "Not logged: it's device teardown, not part of the
	// packet sequence any golden vector or invariant test asserts against"), so
	// "was CloseHandle invoked before the second open" isn't independently
	// observable at this test tier -- that half of the fd-leak fix
	// (AlienFX_SDK.cpp's `if (devHandle) CloseHandle(devHandle);`, right before
	// AlienFXInitialize's enumeration loop) is verified by code inspection, not
	// here. What this test does verify: re-initializing an already-open Functions
	// object on a second, different device works correctly end to end, rather
	// than leaving stale state or crashing.
	ResetAll();
	alienfx_test::SetOpenNodeSucceeds(true);
	Functions f;

	alienfx_test::SetEnumNodes({MakeNode("/dev/hidraw0", 0x187c, 0x0551, 0, 34)});
	ASSERT_TRUE(f.AlienFXInitialize(0, 0));
	ASSERT_EQ(alienfx_test::OpenNodeSuccessCount(), 1u);

	alienfx_test::SetEnumNodes({MakeNode("/dev/hidraw1", 0x187c, 0x0552, 0, 34)});
	ASSERT_TRUE(f.AlienFXInitialize(0, 0));
	EXPECT_EQ(f.pid, 0x0552);
	EXPECT_EQ(alienfx_test::OpenNodeSuccessCount(), 2u);
	ExpectNoTransportActivity();
}

TEST(Detection, InitializeWithNoCandidatesReturnsFalseNotStaleTrue) {
	// AlienFX_SDK.cpp's comment on this: unlike the Windows arm (which relies
	// entirely on AlienFXProbeDevice's own reset and so never resets `version`
	// when its enumeration loop calls ProbeDevice zero times), the Linux arm
	// resets `version` up front -- so a second call that finds nothing returns
	// false rather than echoing the first call's stale success.
	ResetAll();
	alienfx_test::SetOpenNodeSucceeds(true);
	Functions f;

	alienfx_test::SetEnumNodes({MakeNode("/dev/hidraw0", 0x187c, 0x0550, 0, 34)});
	ASSERT_TRUE(f.AlienFXInitialize(0, 0));
	ASSERT_EQ(f.version, AlienFX_SDK::API_V4);

	alienfx_test::SetEnumNodes({});
	EXPECT_FALSE(f.AlienFXInitialize(0, 0));
	EXPECT_EQ(f.version, AlienFX_SDK::API_UNKNOWN);
	ExpectNoTransportActivity();
}
