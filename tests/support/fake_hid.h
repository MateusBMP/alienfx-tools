#pragma once
// M2a's recording fake transport. Implements the same symbols hid_backend.h
// declares (AlienFX-SDK/AlienFX_SDK/hid_backend.h), so it links against
// AlienFX_SDK.cpp's `Functions` class exactly like a real backend would -- but
// instead of touching a device, every call is appended to an in-memory log a test
// can inspect. See Doc/linux_roadmap/16-testing-and-validation.md, "The recording
// fake transport (M2a)".
//
// "Get"-style calls (HidD_GetFeature/HidD_GetInputReport) return a scriptable
// response: QueueRead() pushes one canned response consumed by the next such call;
// when the queue is empty, a default response is used that reports "ready" on
// every status convention this SDK checks at once (byte[0]=ALIENFX_V2_READY,
// byte[2]=ALIENFX_V4_READY) so WaitForReady's polling loops exit after exactly one
// call without a test having to know which internal function is polling.
// WaitForBusy's V4 branch is bypassed entirely by setting a test device's pid to
// 0x551 (the "patch for newer v4" escape hatch already in AlienFX_SDK.cpp) --
// deliberately reusing that hatch is simpler and more honest than teaching the
// fake to distinguish WaitForReady callers from WaitForBusy callers, which the
// real transport has no way to do either.

#include <cstdint>
#include <deque>
#include <ostream>
#include <string>
#include <vector>

namespace alienfx_test {

enum class TransportKind {
	Out,     // HidD_SetOutputReport
	Feat,    // HidD_SetFeature
	Write,   // WriteFile (interrupt)
	Read,    // ReadFile (interrupt)
	GetFeat, // HidD_GetFeature
	GetIn,   // HidD_GetInputReport
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

class FakeHidTransport {
public:
	// Clears the call log and any queued read responses. Call between test cases;
	// gen_golden and packet_builder_test both do this before every PacketCase.
	void Reset();

	// Pushes one canned response for the next Get*-style call to consume. Bytes
	// beyond `length` are ignored, bytes short of `length` are zero-padded --
	// matching how a real HID transport always returns exactly `length` bytes.
	void QueueRead(std::vector<uint8_t> response);

	const std::vector<TransportEvent>& Log() const { return log_; }

	// --- Called by the free functions below, not directly by test code. ---
	bool SetOutput(const uint8_t* buffer, unsigned length);
	bool SetFeature(const uint8_t* buffer, unsigned length);
	bool Write(const uint8_t* buffer, unsigned length);
	bool Read(uint8_t* buffer, unsigned length);
	bool GetFeature(uint8_t* buffer, unsigned length);
	bool GetInputReport(uint8_t* buffer, unsigned length);
	void RecordSleep(unsigned ms);

private:
	bool RecordWrite(TransportKind kind, const uint8_t* buffer, unsigned length);
	bool ServiceRead(TransportKind kind, uint8_t* buffer, unsigned length);

	std::vector<TransportEvent> log_;
	std::deque<std::vector<uint8_t>> readQueue_;
};

// The one fake instance every test links against. hid_backend.h's free functions
// (implemented in fake_hid.cpp) all delegate to this.
FakeHidTransport& GetFakeTransport();

} // namespace alienfx_test
