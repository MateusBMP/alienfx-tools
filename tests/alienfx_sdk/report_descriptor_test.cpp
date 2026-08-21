// M2c: tests for hid_report_descriptor.cpp, the HID report-descriptor parser
// Finding 1's fix depends on (Doc/linux_roadmap/local/test-machine.md). Links only
// alienfx::hid_descriptor -- no hidapi, no AlienFX_SDK, in this binary's dependency
// graph at all (see tests/CMakeLists.txt's report_descriptor_test target).

#include <cstdint>
#include <initializer_list>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "hid_report_descriptor.h"

using alienfx_hid::HidCaps;
using alienfx_hid::ParseHidReportDescriptor;

namespace {

bool Parse(const std::vector<uint8_t>& bytes, HidCaps* out) {
	return ParseHidReportDescriptor(bytes.data(), bytes.size(), out);
}

// A minimal HID short-item descriptor builder, scoped to exactly what these tests
// need: Usage Page / Usage / Collection / Report ID / Report Size / Report Count /
// Input|Output|Feature / End Collection / Push / Pop. Not a general HID authoring
// tool -- e.g. it never emits Logical Min/Max, which real descriptors always have but
// this parser (by design, see hid_report_descriptor.h) never reads.
class DescriptorBuilder {
public:
	// tag/type per HID 1.11 6.2.2.2 (Main=0, Global=1, Local=2); bSize is inferred
	// from data.size() (0/1/2/4 bytes -- HID's 2-bit field, where 3 means 4 bytes).
	DescriptorBuilder& Item(uint8_t tag, uint8_t type, std::initializer_list<uint8_t> data = {}) {
		unsigned n = data.size();
		uint8_t bSize = n == 0 ? 0 : n == 1 ? 1 : n == 2 ? 2 : 3;
		bytes_.push_back(static_cast<uint8_t>((tag << 4) | (type << 2) | bSize));
		for (uint8_t b : data) bytes_.push_back(b);
		return *this;
	}

	DescriptorBuilder& UsagePage(uint8_t v = 0)      { return Item(0x0, 1, {v}); }
	DescriptorBuilder& Usage(uint8_t v = 0)          { return Item(0x0, 2, {v}); }
	DescriptorBuilder& Collection()                  { return Item(0xA, 0, {0x01}); }
	DescriptorBuilder& EndCollection()                { return Item(0xC, 0); }
	DescriptorBuilder& ReportId(uint8_t id)           { return Item(0x8, 1, {id}); }
	DescriptorBuilder& ReportSize(uint8_t v)          { return Item(0x7, 1, {v}); }
	DescriptorBuilder& ReportCount(uint8_t v)         { return Item(0x9, 1, {v}); }
	DescriptorBuilder& Input()                        { return Item(0x8, 0, {0x00}); }
	DescriptorBuilder& Output()                       { return Item(0x9, 0, {0x00}); }
	DescriptorBuilder& Feature()                      { return Item(0xB, 0, {0x00}); }
	DescriptorBuilder& Push()                         { return Item(0xA, 1); }
	DescriptorBuilder& Pop()                          { return Item(0xB, 1); }
	DescriptorBuilder& LongItem(uint8_t tag, std::initializer_list<uint8_t> data) {
		bytes_.push_back(0xFE);
		bytes_.push_back(static_cast<uint8_t>(data.size()));
		bytes_.push_back(tag);
		for (uint8_t b : data) bytes_.push_back(b);
		return *this;
	}
	DescriptorBuilder& Raw(std::initializer_list<uint8_t> raw) {
		bytes_.insert(bytes_.end(), raw);
		return *this;
	}

	const std::vector<uint8_t>& Bytes() const { return bytes_; }

private:
	std::vector<uint8_t> bytes_;
};

// A simple Application collection with one report of the given kind, size and count,
// no Report ID item (an unnumbered report -- matches the test machine's real V4
// descriptor).
std::vector<uint8_t> SimpleReport(char kind, uint8_t reportSize, uint8_t reportCount) {
	DescriptorBuilder b;
	b.UsagePage().Usage().Collection().ReportSize(reportSize).ReportCount(reportCount);
	if (kind == 'O') b.Output();
	else if (kind == 'F') b.Feature();
	else b.Input();
	b.EndCollection();
	return b.Bytes();
}

} // namespace

// --- The exact bytes Finding 1 is about ------------------------------------------

TEST(HidReportDescriptor, TestMachineV4DescriptorResolvesTo34) {
	// Doc/linux_roadmap/local/test-machine.md's Finding 1: this fork's Alienware
	// m15 R6 light controller (187c:0550), read verbatim from
	// /sys/bus/hid/devices/0003:187C:0550.0004/report_descriptor. Report Size 8,
	// Report Count 0x21 (33), no Report ID item -- Windows' OutputReportByteLength
	// there is 34 (the leading report-ID byte Linux's raw Report Count omits).
	std::vector<uint8_t> descriptor = {
		0x06, 0x00, 0xff,             // Usage Page (vendor 0xFF00)
		0x09, 0x01,                   // Usage (1)
		0xa1, 0x01,                   // Collection (Application)
		0x15, 0x00,                   //   Logical Minimum (0)
		0x26, 0xff, 0x00,             //   Logical Maximum (255)
		0x75, 0x08,                   //   Report Size (8)
		0x95, 0x21,                   //   Report Count (33)
		0x09, 0x01,                   //   Usage (1)
		0x81, 0x00,                   //   Input
		0x09, 0x01,                   //   Usage (1)
		0x91, 0x00,                   //   Output
		0xc0,                         // End Collection
	};
	HidCaps caps;
	ASSERT_TRUE(Parse(descriptor, &caps));
	EXPECT_EQ(caps.outputReportByteLength, 34); // Finding 1's regression assertion
	EXPECT_EQ(caps.featureReportByteLength, 0);
}

// --- Synthesized single-report descriptors, other API versions -------------------

TEST(HidReportDescriptor, EightByteOutputResolvesTo9_V2) {
	HidCaps caps;
	ASSERT_TRUE(Parse(SimpleReport('O', /*size=*/8, /*count=*/8), &caps));
	EXPECT_EQ(caps.outputReportByteLength, 9);
}

TEST(HidReportDescriptor, ElevenByteOutputResolvesTo12_V3) {
	HidCaps caps;
	ASSERT_TRUE(Parse(SimpleReport('O', /*size=*/8, /*count=*/11), &caps));
	EXPECT_EQ(caps.outputReportByteLength, 12);
}

TEST(HidReportDescriptor, SixtyFourByteOutputResolvesTo65_V6) {
	HidCaps caps;
	ASSERT_TRUE(Parse(SimpleReport('O', /*size=*/8, /*count=*/64), &caps));
	EXPECT_EQ(caps.outputReportByteLength, 65);
}

// --- The conditional +1: zero must stay zero --------------------------------------

TEST(HidReportDescriptor, NoOutputOrFeatureReportGivesZeroNotOne) {
	// The bug the first draft of this plan had: an unconditional "+1" would make
	// this 1, not 0 -- and API_V5's `!length` detection condition depends on this
	// exact zero being reachable. A collection with only an Input report has no
	// Output report at all.
	HidCaps caps;
	ASSERT_TRUE(Parse(SimpleReport('I', /*size=*/8, /*count=*/8), &caps));
	EXPECT_EQ(caps.outputReportByteLength, 0);
	EXPECT_EQ(caps.featureReportByteLength, 0);
}

// --- Report ID handling: max-across-IDs, not sum, and the "85 xx" item -------------

TEST(HidReportDescriptor, MultipleReportIdsTakeMaxNotSum) {
	DescriptorBuilder b;
	b.UsagePage().Usage().Collection()
	 .ReportId(1).ReportSize(8).ReportCount(4).Output()   // ID 1: 32 bits -> len 5
	 .ReportId(2).ReportSize(8).ReportCount(10).Output()  // ID 2: 80 bits -> len 11
	 .EndCollection();
	HidCaps caps;
	ASSERT_TRUE(Parse(b.Bytes(), &caps));
	EXPECT_EQ(caps.outputReportByteLength, 11); // max(5, 11), not 5+11
}

// --- Push/Pop: global state must be saved and restored, not leaked ----------------

TEST(HidReportDescriptor, PushPopRestoresReportCount) {
	DescriptorBuilder b;
	b.UsagePage().Usage().Collection()
	 .ReportSize(8).ReportCount(5)
	 .Push()
	 .ReportCount(100) // must not affect the Output emitted after Pop restores 5
	 .Pop()
	 .Output()
	 .EndCollection();
	HidCaps caps;
	ASSERT_TRUE(Parse(b.Bytes(), &caps));
	EXPECT_EQ(caps.outputReportByteLength, 6); // (8*5=40 bits -> ceil=5) + 1
}

TEST(HidReportDescriptor, UnmatchedPopIsMalformed) {
	DescriptorBuilder b;
	b.UsagePage().Usage().Collection().Pop().ReportSize(8).ReportCount(1).Output().EndCollection();
	HidCaps caps;
	EXPECT_FALSE(Parse(b.Bytes(), &caps));
}

// --- Finding 2's shape: whole-node aggregation sees the sibling collection --------

TEST(HidReportDescriptor, DarfonCompositeNodeHasNonZeroOutputDespiteVendorCollection) {
	// Two sibling top-level collections in one descriptor, matching what a single
	// hidraw node exposes for a composite device (Doc/linux_roadmap/
	// local/test-machine.md's Finding 2): a vendor-usage collection with only a
	// Feature report (7 bytes, Report ID 0xCC -- this fork's actual Darfon
	// keyboard shape) and a sibling boot-keyboard collection with an Output report
	// (the caps/numlock/scrolllock LEDs, Report ID 1). Windows would see these as
	// two independent device interface paths; this parser, like real Linux hidraw,
	// sees one node and aggregates across both -- so outputReportByteLength is
	// correctly non-zero here even though the vendor collection alone would report
	// zero. That's what makes a literal port of Windows' `!length` V5 condition
	// never fire on Linux -- an accepted, M2e-owned gap (see hid_report_descriptor.h).
	DescriptorBuilder b;
	b.Raw({0x06, 0x89, 0xff})       // Usage Page (vendor 0xFF89)
	 .Raw({0x09, 0xcc})             // Usage (0xCC) -- the V5 trigger usage
	 .Collection()
	 .ReportId(0xcc).ReportSize(8).ReportCount(7).Feature()
	 .EndCollection()
	 .Collection() // sibling: boot-keyboard LED report
	 .ReportId(1).ReportSize(8).ReportCount(1).Output()
	 .EndCollection();
	HidCaps caps;
	ASSERT_TRUE(Parse(b.Bytes(), &caps));
	EXPECT_NE(caps.outputReportByteLength, 0);
	EXPECT_EQ(caps.featureReportByteLength, 8); // (8*7=56 bits -> ceil=7) + 1
}

// --- Hardening: malformed input must fail closed, never read out of bounds -------

TEST(HidReportDescriptor, TruncatedShortItemIsRejected) {
	// Usage Page declares 2 bytes of data but the buffer ends after the prefix.
	HidCaps caps;
	EXPECT_FALSE(Parse({0x06}, &caps));
}

TEST(HidReportDescriptor, ShortItemDataRunningPastBufferEndIsRejected) {
	// Declares 2 data bytes, only 1 present.
	HidCaps caps;
	EXPECT_FALSE(Parse({0x06, 0x00}, &caps));
}

TEST(HidReportDescriptor, TruncatedLongItemIsRejected) {
	HidCaps caps;
	EXPECT_FALSE(Parse({0xFE}, &caps));       // missing bDataSize, bLongItemTag
	EXPECT_FALSE(Parse({0xFE, 0x02, 0x01}, &caps)); // declares 2 data bytes, 0 present
}

TEST(HidReportDescriptor, LongItemIsSkippedWithoutAffectingParse) {
	DescriptorBuilder b;
	b.UsagePage().Usage().Collection()
	 .LongItem(0x01, {0xAA, 0xBB})
	 .ReportSize(8).ReportCount(8).Output()
	 .EndCollection();
	HidCaps caps;
	ASSERT_TRUE(Parse(b.Bytes(), &caps));
	EXPECT_EQ(caps.outputReportByteLength, 9);
}

TEST(HidReportDescriptor, UnknownMainTagIsIgnoredNotFatal) {
	DescriptorBuilder b;
	b.UsagePage().Usage().Collection()
	 .Item(0xF, 0, {0x00}) // reserved/unrecognized Main tag -- must not be fatal
	 .ReportSize(8).ReportCount(8).Output()
	 .EndCollection();
	HidCaps caps;
	ASSERT_TRUE(Parse(b.Bytes(), &caps));
	EXPECT_EQ(caps.outputReportByteLength, 9);
}

TEST(HidReportDescriptor, UnbalancedCollectionNestingIsRejected) {
	DescriptorBuilder b;
	b.UsagePage().Usage().Collection().ReportSize(8).ReportCount(8).Output();
	// no EndCollection
	HidCaps caps;
	EXPECT_FALSE(Parse(b.Bytes(), &caps));
}

TEST(HidReportDescriptor, ExcessiveCollectionNestingIsRejected) {
	DescriptorBuilder b;
	for (int i = 0; i < 40; ++i) b.Collection(); // well past any real descriptor's depth
	for (int i = 0; i < 40; ++i) b.EndCollection();
	HidCaps caps;
	EXPECT_FALSE(Parse(b.Bytes(), &caps));
}

TEST(HidReportDescriptor, ReportSizeTimesReportCountOverflowGuardRejects) {
	DescriptorBuilder b;
	b.UsagePage().Usage().Collection()
	 .Item(0x7, 1, {0xff, 0xff}) // Report Size = 0xffff
	 .Item(0x9, 1, {0xff, 0xff}) // Report Count = 0xffff
	 .Output()
	 .EndCollection();
	HidCaps caps;
	EXPECT_FALSE(Parse(b.Bytes(), &caps));
}

TEST(HidReportDescriptor, OversizedLengthIsRejectedNotClamped) {
	// 8*250 = 2000 bits -> (2000+7)/8 + 1 = 251, over AlienFX_SDK.h's
	// MAX_BUFFERSIZE (193). Must fail closed, not silently clamp to 193 --
	// PrepareAndSend trusts `length` to size a fixed stack buffer.
	HidCaps caps;
	EXPECT_FALSE(Parse(SimpleReport('O', /*size=*/8, /*count=*/250), &caps));
}

TEST(HidReportDescriptor, EmptyDescriptorIsValidWithZeroCaps) {
	// Not malformed -- a descriptor with no items simply declares no reports.
	// Documented, tested behavior, not an accidental fallthrough.
	HidCaps caps;
	ASSERT_TRUE(Parse({}, &caps));
	EXPECT_EQ(caps.outputReportByteLength, 0);
	EXPECT_EQ(caps.featureReportByteLength, 0);
}
