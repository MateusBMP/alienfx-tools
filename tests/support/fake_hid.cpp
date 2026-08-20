#include "fake_hid.h"

#include "hid_backend.h"

#include <algorithm>

namespace alienfx_test {

void FakeHidTransport::Reset() {
	log_.clear();
	readQueue_.clear();
}

void FakeHidTransport::QueueRead(std::vector<uint8_t> response) {
	readQueue_.push_back(std::move(response));
}

bool FakeHidTransport::RecordWrite(TransportKind kind, const uint8_t* buffer, unsigned length) {
	log_.push_back({kind, std::vector<uint8_t>(buffer, buffer + length), 0});
	return true;
}

bool FakeHidTransport::ServiceRead(TransportKind kind, uint8_t* buffer, unsigned length) {
	std::vector<uint8_t> response;
	if (!readQueue_.empty()) {
		response = std::move(readQueue_.front());
		readQueue_.pop_front();
		response.resize(length, 0);
	} else {
		// Default "always ready" response: satisfies WaitForReady's polling loop
		// on the very first call regardless of which status convention the
		// calling API version checks -- see AlienFX_SDK.h's ALIENFX_V2_READY
		// (0x10, byte[0], APIv1-v3) and ALIENFX_V4_READY (33, byte[2], APIv4).
		response.assign(length, 0);
		if (length > 0) response[0] = 0x10;
		if (length > 2) response[2] = 33;
	}
	std::copy(response.begin(), response.end(), buffer);
	log_.push_back({kind, response, 0});
	return true;
}

bool FakeHidTransport::SetOutput(const uint8_t* buffer, unsigned length) {
	return RecordWrite(TransportKind::Out, buffer, length);
}
bool FakeHidTransport::SetFeature(const uint8_t* buffer, unsigned length) {
	return RecordWrite(TransportKind::Feat, buffer, length);
}
bool FakeHidTransport::Write(const uint8_t* buffer, unsigned length) {
	return RecordWrite(TransportKind::Write, buffer, length);
}
bool FakeHidTransport::Read(uint8_t* buffer, unsigned length) {
	return ServiceRead(TransportKind::Read, buffer, length);
}
bool FakeHidTransport::GetFeature(uint8_t* buffer, unsigned length) {
	return ServiceRead(TransportKind::GetFeat, buffer, length);
}
bool FakeHidTransport::GetInputReport(uint8_t* buffer, unsigned length) {
	return ServiceRead(TransportKind::GetIn, buffer, length);
}
void FakeHidTransport::RecordSleep(unsigned ms) {
	log_.push_back({TransportKind::Sleep, {}, ms});
}

FakeHidTransport& GetFakeTransport() {
	static FakeHidTransport instance;
	return instance;
}

} // namespace alienfx_test

// --- hid_backend.h's free-function seam, delegating to the fake instance --------

BOOL HidD_SetOutputReport(HANDLE, void* reportBuffer, DWORD reportBufferLength) {
	return alienfx_test::GetFakeTransport().SetOutput(
		static_cast<const uint8_t*>(reportBuffer), reportBufferLength);
}
BOOL HidD_SetFeature(HANDLE, void* reportBuffer, DWORD reportBufferLength) {
	return alienfx_test::GetFakeTransport().SetFeature(
		static_cast<const uint8_t*>(reportBuffer), reportBufferLength);
}
BOOL HidD_GetFeature(HANDLE, void* reportBuffer, DWORD reportBufferLength) {
	return alienfx_test::GetFakeTransport().GetFeature(
		static_cast<uint8_t*>(reportBuffer), reportBufferLength);
}
BOOL HidD_GetInputReport(HANDLE, void* reportBuffer, DWORD reportBufferLength) {
	return alienfx_test::GetFakeTransport().GetInputReport(
		static_cast<uint8_t*>(reportBuffer), reportBufferLength);
}

BOOL WriteFile(HANDLE, const void* buffer, DWORD numberOfBytesToWrite,
               DWORD* numberOfBytesWritten, void*) {
	bool ok = alienfx_test::GetFakeTransport().Write(
		static_cast<const uint8_t*>(buffer), numberOfBytesToWrite);
	if (numberOfBytesWritten) *numberOfBytesWritten = numberOfBytesToWrite;
	return ok;
}
BOOL ReadFile(HANDLE, void* buffer, DWORD numberOfBytesToRead,
              DWORD* numberOfBytesRead, void*) {
	bool ok = alienfx_test::GetFakeTransport().Read(
		static_cast<uint8_t*>(buffer), numberOfBytesToRead);
	if (numberOfBytesRead) *numberOfBytesRead = numberOfBytesToRead;
	return ok;
}

BOOL CloseHandle(HANDLE) {
	// Not logged: it's device teardown (~Functions), not part of the packet
	// sequence any golden vector or invariant test asserts against.
	return TRUE;
}
void Sleep(DWORD milliseconds) {
	alienfx_test::GetFakeTransport().RecordSleep(milliseconds);
}
