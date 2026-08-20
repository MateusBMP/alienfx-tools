#pragma once
// M2b's recording fake for the *hidapi* C API (as opposed to fake_hid.h's M2a
// fake, which backs hid_backend.h's Windows-shaped HidD_*/WriteFile/ReadFile
// seam directly). This one stubs the handful of hidapi calls
// AlienFX-SDK/AlienFX_SDK/hid_backend_linux.cpp makes, so that file -- the real
// backend M2b ships -- can be linked into a test binary and driven through
// tests/support/packet_matrix.h's shared matrix without ever touching hidapi's
// real hidraw implementation or a physical device. See
// Doc/linux_roadmap/16-testing-and-validation.md's fake-hidapi tier.
//
// Records into the same TransportEvent shape transport_log.h defines (shared
// with fake_hid.h) and reuses the exact same default-response / QueueRead
// scripting behavior fake_hid.h's FakeHidTransport::ServiceRead documents, so
// tests/alienfx_sdk/transport_backend_test.cpp replays the *same* golden
// vectors under tests/golden/alienfx_sdk/ that tests/alienfx_sdk/
// packet_builder_test.cpp (M2a) checks against fake_hid.h -- proving
// hid_backend_linux.cpp's hidapi call mapping produces byte-identical output.
//
// Unlike fake_hid.h, calls here are keyed by an opaque hid_device* (a fake
// "device handle" a test invents, e.g. reinterpret_cast<hid_device*>(0x1), same
// idiom tests/alienfx_sdk/packet_builder_test.cpp already uses for HANDLE) --
// hid_backend_linux.cpp's vendor-allowlist gate needs a VID attached to that
// handle, via either alienfx_hid::RegisterDevice() (its own registry) or this
// fake's SetDeviceInfo() (which backs hid_get_device_info(), the gate's
// fallback when a handle wasn't registered).

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

#include <hidapi.h>

#include "transport_log.h"

namespace alienfx_test {

class FakeHidapiTransport {
public:
	// Clears the call log, any queued read responses, and all registered device
	// info. Call between test cases, same convention as fake_hid.h's Reset().
	void Reset();

	// See fake_hid.h's QueueRead(): scripts the next Get*/ReadTimeout response;
	// when the queue is empty, the same "always ready" default response is used
	// (byte[0]=ALIENFX_V2_READY, byte[2]=ALIENFX_V4_READY) so WaitForReady's
	// polling loops exit after exactly one call.
	void QueueRead(std::vector<uint8_t> response);

	const std::vector<TransportEvent>& Log() const { return log_; }

	// Captures the caller-supplied buffer[0] ("requested report ID") for the most
	// recent GetFeatureReport/GetInputReport call, before it's overwritten by the
	// scripted response. Not part of TransportEvent/the logged Log() above --
	// that stays purely about response-side bytes, to match golden-vector
	// semantics shared with fake_hid.h. This exists so a test can assert on
	// AlienFX_SDK.cpp's defect-1 fix (GetDeviceStatus now sets buffer[0] to
	// reportIDList[version] before every Get* call) without changing that shape.
	uint8_t LastRequestedReportId() const { return lastRequestedReportId_; }

	// Backs hid_get_device_info() for a given fake handle -- this is what
	// hid_backend_linux.cpp's allowlist gate falls back to when a handle wasn't
	// registered via alienfx_hid::RegisterDevice.
	void SetDeviceInfo(hid_device* dev, uint16_t vid, uint16_t pid);

	// --- Called by the free hidapi functions below, not directly by test code. ---
	int SendOutputReport(hid_device* dev, const uint8_t* data, size_t length);
	int SendFeatureReport(hid_device* dev, const uint8_t* data, size_t length);
	int Write(hid_device* dev, const uint8_t* data, size_t length);
	int ReadTimeout(hid_device* dev, uint8_t* data, size_t length, int milliseconds);
	int GetFeatureReport(hid_device* dev, uint8_t* data, size_t length);
	int GetInputReport(hid_device* dev, uint8_t* data, size_t length);
	void Close(hid_device* dev);
	const hid_device_info* DeviceInfo(hid_device* dev);
	void RecordSleep(unsigned ms);

private:
	int RecordWrite(TransportKind kind, const uint8_t* buffer, size_t length);
	int ServiceRead(TransportKind kind, uint8_t* buffer, size_t length);

	std::vector<TransportEvent> log_;
	std::deque<std::vector<uint8_t>> readQueue_;
	std::unordered_map<hid_device*, hid_device_info> deviceInfo_;
	uint8_t lastRequestedReportId_ = 0;
};

// The one fake instance every transport-backend test links against.
// hid_backend_linux.cpp's hidapi calls (implemented in fake_hidapi.cpp) all
// delegate to this. Installs itself as hid_backend_linux.h's Sleep observer on
// first use -- see fake_hidapi.cpp.
FakeHidapiTransport& GetFakeHidapi();

} // namespace alienfx_test
