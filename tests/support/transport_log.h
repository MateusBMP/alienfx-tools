#pragma once
// The transport-event vocabulary shared by every fake transport in this test tree
// and by tests/support/golden_vector.{h,cpp}'s file format. Split out of fake_hid.h
// (M2a) when tests/support/fake_hidapi.{h,cpp} (M2b) needed the same
// TransportKind/TransportEvent types without depending on fake_hid.h's
// FakeHidTransport class, which is specific to the HidD_*/WriteFile/ReadFile seam
// (hid_backend.h) -- fake_hidapi.h instead backs the hidapi C API seam
// (hid_backend_linux.cpp), but both record into this same event shape so
// golden_vector.{h,cpp} and the golden files in tests/golden/alienfx_sdk/ are
// identical for either fake.

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace alienfx_test {

enum class TransportKind {
	Out,     // HidD_SetOutputReport / hid_send_output_report
	Feat,    // HidD_SetFeature / hid_send_feature_report
	Write,   // WriteFile / hid_write (interrupt)
	Read,    // ReadFile / hid_read_timeout (interrupt)
	GetFeat, // HidD_GetFeature / hid_get_feature_report
	GetIn,   // HidD_GetInputReport / hid_get_input_report
	Sleep,   // Sleep(ms) -- bytes empty, sleepMs set
};

const char* ToToken(TransportKind kind);
bool FromToken(const std::string& token, TransportKind* outKind);

struct TransportEvent {
	TransportKind kind;
	std::vector<uint8_t> bytes;
	unsigned sleepMs = 0;

	bool operator==(const TransportEvent& other) const {
		return kind == other.kind && bytes == other.bytes && sleepMs == other.sleepMs;
	}
	bool operator!=(const TransportEvent& other) const { return !(*this == other); }
};

// GTest/GMock look this up via ADL in TransportEvent's own namespace.
void PrintTo(const TransportEvent& event, std::ostream* os);

} // namespace alienfx_test
