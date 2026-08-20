// Hand-derived tier (Doc/linux_roadmap/16-testing-and-validation.md): expected
// bytes computed independently from alienfx-controls.h's constants and
// Doc/linux_roadmap/04-alienfx-sdk-hid.md's documented formulas -- NOT loaded from
// a golden file and not derived by running the builder and capturing its output.
// Covers the four spots doc 04/16 call fragile: the V2 4-bit color packing, the V6
// XOR checksum, V7's write-then-read transport order, and V8's
// feature-vs-interrupt size heuristic. Every byte value below is computed by hand
// in the accompanying comment, from AlienFX_SDK.cpp's documented algorithm
// (PrepareAndSend's memset+memcpy+patch, doc 04's "The transport core") -- not by
// calling into AlienFX_SDK.cpp itself to see what it produces.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "AlienFX_SDK.h"
#include "fake_hid.h"

using AlienFX_SDK::Afx_action;
using AlienFX_SDK::Afx_lightblock;
using AlienFX_SDK::Functions;
using ::testing::ElementsAreArray;

namespace {

Functions MakeDevice(int version, int length, unsigned short pid = 0) {
	Functions f;
	f.version = version;
#ifdef ALIENFX_TESTING
	f.TestSetDeviceState(reinterpret_cast<HANDLE>(0x1), length, 0, pid);
#endif
	alienfx_test::GetFakeTransport().Reset();
	return f;
}

TEST(ProtocolInvariants, V2FourBitColorPacking) {
	// AlienFX_SDK.cpp's SetMaskAndColor, API_V2 case (04-alienfx-sdk-hid.md:219-222):
	//   byte1 = (c1.r & 0xf0) | ((c1.g & 0xf0) >> 4)
	//   byte2 = (c1.b & 0xf0) | ((c2.r & 0xf0) >> 4)
	//   byte3 = (c2.g & 0xf0) | ((c2.b & 0xf0) >> 4)
	// With c1 = {r=0xAB, g=0x7C, b=0x3D} and c2 all-zero (single-phase action):
	//   byte1 = 0xA0 | (0x70>>4)=0x07            = 0xA7
	//   byte2 = 0x30 | (0x00>>4)=0x00            = 0x30
	//   byte3 = 0x00 | 0x00                       = 0x00
	Functions f = MakeDevice(AlienFX_SDK::API_V2, 9);

	f.SetColor(0, Afx_action{(BYTE)AlienFX_SDK::AlienFX_A_Color, 0, 0, 0xAB, 0x7C, 0x3D});

	// Full sequence, independently traced through AlienFX_SDK.cpp's control flow
	// (SetColor -> SetAction -> Reset (COMMV1_reset, then WaitForReady's status
	// poll) -> the color packet carrying the nibble-packed bytes -> COMMV1_loop):
	const std::vector<alienfx_test::TransportEvent> expected = {
		{alienfx_test::TransportKind::Out,   {0x02, 0x07, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 0}, // COMMV1_reset
		{alienfx_test::TransportKind::Out,   {0x02, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 0}, // COMMV1_status (WaitForReady poll)
		{alienfx_test::TransportKind::GetIn, {0x10, 0x00, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 0}, // fake's default "ready" response
		{alienfx_test::TransportKind::Out,   {0x02, 0x03, 0x01, 0x00, 0x00, 0x01, 0xA7, 0x30, 0x00}, 0}, // COMMV1_color -- bytes 6-8 are the packed nibbles under test
		{alienfx_test::TransportKind::Out,   {0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 0}, // COMMV1_loop
	};
	EXPECT_THAT(alienfx_test::GetFakeTransport().Log(), ElementsAreArray(expected));
}

TEST(ProtocolInvariants, V6XorChecksum) {
	// AlienFX_SDK.cpp's SetMaskAndColor, API_V6 case (04-alienfx-sdk-hid.md:318-326):
	//   mods = {{9, {index, c1.r, c1.g, c1.b}}}
	//   mask = c1.r ^ c1.g ^ c1.b ^ index, then (for a plain color action) mask ^= 8
	//   mods += {13, {bright, mask}}
	// With index=0 (SetMaskAndColor's default DWORD param), c1={r=0x11,g=0x22,b=0x33},
	// bright=64 (0x40, Functions' default, unchanged since SetBrightness wasn't
	// called):
	//   mask = 0x11 ^ 0x22 ^ 0x33 ^ 0 = 0x00, then ^= 8 -> 0x08
	Functions f = MakeDevice(AlienFX_SDK::API_V6, 65);

	f.SetColor(0, Afx_action{(BYTE)AlienFX_SDK::AlienFX_A_Color, 0, 0, 0x11, 0x22, 0x33});

	// V6's buffer is prefilled 0xff (PrepareAndSend: `memset(buffer, version ==
	// API_V6 ? 0xff : 0, length)`), reportID is 0 (reportIDList[API_V6] == 0), and
	// V6 has no WaitForReady polling in Reset() -- just the reset packet, then the
	// color packet carrying the checksum under test.
	std::vector<uint8_t> resetBuf(65, 0xff);
	resetBuf[0] = 0x00; resetBuf[1] = 0x95; resetBuf[2] = 0x00; resetBuf[3] = 0x00; resetBuf[4] = 0x00;

	std::vector<uint8_t> colorBuf(65, 0xff);
	colorBuf[0] = 0x00; colorBuf[1] = 0x92; colorBuf[2] = 0x37;
	colorBuf[9] = 0x00; colorBuf[10] = 0x11; colorBuf[11] = 0x22; colorBuf[12] = 0x33; // {9,{index,c1.r,c1.g,c1.b}}
	colorBuf[13] = 0x40; colorBuf[14] = 0x08;                                          // {13,{bright,mask}} -- mask is the checksum under test

	const std::vector<alienfx_test::TransportEvent> expected = {
		{alienfx_test::TransportKind::Write, resetBuf, 0},
		{alienfx_test::TransportKind::Write, colorBuf, 0},
	};
	EXPECT_THAT(alienfx_test::GetFakeTransport().Log(), ElementsAreArray(expected));
}

TEST(ProtocolInvariants, V7WriteThenReadTransportOrder) {
	// PrepareAndSend's API_V7 branch (04-alienfx-sdk-hid.md's transport table):
	// every send is a WriteFile immediately followed by a ReadFile, unconditionally
	// -- "the device appears to require the read to complete the transaction".
	// This is a structural claim about transport order, independent of packet
	// content, so it's asserted on event *kinds* rather than exact bytes. V7 has no
	// Reset()-time traffic (Reset()'s switch has no API_V7 case, so no packet is
	// sent there), so SetColor produces exactly one PrepareAndSend call.
	Functions f = MakeDevice(AlienFX_SDK::API_V7, 65);

	f.SetColor(0, Afx_action{(BYTE)AlienFX_SDK::AlienFX_A_Color, 0, 0, 0x10, 0x20, 0x30});

	const auto& log = alienfx_test::GetFakeTransport().Log();
	ASSERT_EQ(log.size(), 2u);
	EXPECT_EQ(log[0].kind, alienfx_test::TransportKind::Write);
	EXPECT_EQ(log[1].kind, alienfx_test::TransportKind::Read);
}

TEST(ProtocolInvariants, V8FeatureVsInterruptSizeHeuristic) {
	// PrepareAndSend's API_V8 branch: `needV8Feature = mods->front().vval.size() ==
	// 1` -- a single-byte patch goes via HidD_SetFeature (wrapped in Sleep(4)/
	// Sleep(6)); anything else goes via the interrupt WriteFile. SetColor's V8 path
	// (AlienFX_SDK.cpp's SetAction, API_V8 case) exercises both branches in one
	// call: a bare `PrepareAndSend(COMMV8_readyToColor)` with mods == nullptr
	// leaves `needV8Feature` at its declared-true default (the mods-patching block
	// that would set it false never runs when mods is null) -> feature path; the
	// second call carries AddV8DataBlock's 13-byte patch -> size != 1 -> interrupt
	// write path.
	Functions f = MakeDevice(AlienFX_SDK::API_V8, 65, 0);

	f.SetColor(0, Afx_action{(BYTE)AlienFX_SDK::AlienFX_A_Color, 0, 0, 0x44, 0x55, 0x66});

	const auto& log = alienfx_test::GetFakeTransport().Log();
	ASSERT_EQ(log.size(), 4u);
	EXPECT_EQ(log[0].kind, alienfx_test::TransportKind::Sleep);
	EXPECT_EQ(log[0].sleepMs, 4u);
	EXPECT_EQ(log[1].kind, alienfx_test::TransportKind::Feat);
	EXPECT_EQ(log[2].kind, alienfx_test::TransportKind::Sleep);
	EXPECT_EQ(log[2].sleepMs, 6u);
	EXPECT_EQ(log[3].kind, alienfx_test::TransportKind::Write);

	// The feature packet is the bare COMMV8_effectReady... no -- COMMV8_readyToColor
	// template {4,0xe,0x1,0x0,0x1} with no patches: reportID (reportIDList[API_V8])
	// is 1, so buffer[0..4] = {1,0xe,1,0,1}.
	ASSERT_GE(log[1].bytes.size(), 5u);
	EXPECT_THAT(std::vector<uint8_t>(log[1].bytes.begin(), log[1].bytes.begin() + 5),
	            ElementsAreArray({0x01, 0x0e, 0x01, 0x00, 0x01}));

	// The write packet's AddV8DataBlock patch at offset 5: {index=0,
	// opcode=v8OpCodes[AlienFX_A_Color]=0x81, tempo=0, 0xa5, time=0, 0xa,
	// r=0x44,g=0x55,b=0x66, r2=0x44,g2=0x55,b2=0x66 (front()==back(), one-phase
	// action), 2}.
	ASSERT_GE(log[3].bytes.size(), 18u);
	EXPECT_THAT(std::vector<uint8_t>(log[3].bytes.begin() + 5, log[3].bytes.begin() + 18),
	            ElementsAreArray({0x00, 0x81, 0x00, 0xa5, 0x00, 0x0a,
	                               0x44, 0x55, 0x66, 0x44, 0x55, 0x66, 0x02}));
}

} // namespace
