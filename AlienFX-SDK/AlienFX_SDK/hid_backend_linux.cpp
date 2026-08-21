// M2b: the real implementation of hid_backend.h's transport seam, over hidapi.
// Doc/linux_roadmap/04-alienfx-sdk-hid.md's "hidraw / hidapi mapping" table is the
// design source for the eight-symbol mapping below; hid_backend_linux.h documents
// the control surface (dry run, vendor allowlist, output-report mode).
//
// This file links against hidapi's *headers only* (target alienfx::hid_linux
// depends on hidapi::include, not hidapi::hidraw) -- the actual hidapi
// implementation is supplied by whoever links the final binary: the real
// libhidapi-hidraw for a shipped binary, or tests/support/fake_hidapi.cpp (M2b's
// recording stub, mirroring M2a's fake_hid.cpp) for
// tests/alienfx_sdk/transport_backend_test.cpp. That test replays M2a's golden
// vectors through *this exact file*, proving the hidapi call mapping is correct
// without ever opening a real device -- device opening is M2c's, not this file's;
// nothing here calls hid_open()/hid_open_path()/hid_enumerate().
//
// Four porting defects this file's design addresses (Doc/linux_roadmap/
// 04-alienfx-sdk-hid.md has the full writeup):
//  1. Get*-style calls need buffer[0] pre-set to the requested report ID --
//     that's AlienFX_SDK.cpp's GetDeviceStatus, fixed there (three additive
//     #ifndef _WIN32 lines), not here; this file just passes the caller's buffer
//     through unchanged.
//  2. hid_read_timeout()'s 0 == timeout / -1 == error split, mapped onto
//     ReadFile's TRUE-with-zero-bytes-on-timeout Windows contract below.
//  3. HidD_SetOutputReport is a control-endpoint Set_Report transfer;
//     hid_send_output_report matches it but only exists since hidapi 0.15.0 --
//     older hidapi falls back to hid_write (an interrupt transfer, genuinely
//     different on the wire), and ALIENFX_HID_OUTPUT_MODE picks between the two
//     at runtime once hidapi >= 0.15.0 is available, so M2d can settle this
//     against real V2/V3/V4 hardware without a rebuild.
//  4. fake_hidapi.cpp and real libhidapi can never both be the link provider of
//     the same symbols -- enforced by CMake (tests/CMakeLists.txt), not by this
//     file.

#include "hid_backend_linux.h"

#include "hid_backend.h"

#include <hidapi.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace {

// --- Internal state, private to this translation unit. Deliberately NOT nested
// inside `namespace alienfx_hid` -- an anonymous namespace nested in a named one
// makes its members reachable unqualified only from within that named namespace,
// which would force every helper below to be re-declared or forwarded just to be
// callable from this file's global-namespace hid_backend.h wrappers. A single
// file-scope anonymous namespace is visible, unqualified, from both
// `namespace alienfx_hid { ... }` below and the global-namespace wrappers at the
// bottom of this file, with no forwarding needed. -------------------------------

struct DeviceInfo {
	uint16_t vid = 0;
	uint16_t pid = 0;
};

std::mutex g_deviceMutex;
std::unordered_map<HANDLE, DeviceInfo> g_devices;

std::atomic<bool> g_dryRun{false};
std::atomic<bool> g_allowAnyVendor{false};
std::atomic<alienfx_hid::OutputMode> g_outputMode{alienfx_hid::OutputMode::Report};
std::mutex g_sinkMutex;
std::ostream* g_sink = &std::cout;

std::once_flag g_envSeeded;
alienfx_hid::SleepObserver g_sleepObserver = nullptr;

constexpr int kReadTimeoutMs = 100; // mirrors AlienFX_SDK.cpp's COMMTIMEOUTS
                                     // ReadIntervalTimeout ({100,0,0,10,200}, :180,:252)

bool EnvFlagSet(const char* name) {
	const char* v = std::getenv(name);
	return v && std::string_view(v) == "1";
}

void SeedFromEnvironmentOnce() {
	std::call_once(g_envSeeded, [] {
		if (EnvFlagSet("ALIENFX_DRY_RUN")) g_dryRun = true;
		if (EnvFlagSet("ALIENFX_ALLOW_ANY_VENDOR")) g_allowAnyVendor = true;
		if (const char* mode = std::getenv("ALIENFX_HID_OUTPUT_MODE")) {
			if (std::string_view(mode) == "write") g_outputMode = alienfx_hid::OutputMode::Write;
			else if (std::string_view(mode) == "report") g_outputMode = alienfx_hid::OutputMode::Report;
			// Any other value: keep the compiled-in default rather than silently
			// guessing what an unrecognized value meant.
		}
	});
}

DeviceInfo LookupDevice(HANDLE handle) {
	{
		std::lock_guard<std::mutex> lock(g_deviceMutex);
		auto it = g_devices.find(handle);
		if (it != g_devices.end())
			return it->second;
	}
	// Not registered (a caller that skipped alienfx_hid::RegisterDevice) -- ask
	// hidapi itself. This does no I/O: hid_get_device_info returns a struct
	// cached on the hid_device at open time.
	if (handle) {
		if (auto* info = hid_get_device_info(static_cast<hid_device*>(handle)))
			return {info->vendor_id, info->product_id};
	}
	return {};
}

std::ostream& Sink() {
	std::lock_guard<std::mutex> lock(g_sinkMutex);
	return *g_sink;
}

// Refuses a write unless the handle's registered/queried VID is a known AlienFX
// vendor, or the allowlist is explicitly bypassed. This is the only enforcement
// point for Doc/linux_roadmap/15-packaging-and-permissions.md's vendor list --
// every call site that can put caller-controlled bytes on the wire goes through
// it (HidD_SetOutputReport, HidD_SetFeature, WriteFile's interrupt write).
bool CheckAllowlist(HANDLE handle, const char* callName) {
	if (g_allowAnyVendor)
		return true;
	const DeviceInfo info = LookupDevice(handle);
	if (alienfx_hid::IsKnownVendor(info.vid))
		return true;
	Sink() << "alienfx: refusing " << callName << " -- handle's vendor 0x" << std::hex
	       << std::setw(4) << std::setfill('0') << info.vid << std::dec
	       << " is not a known AlienFX-family vendor"
	          " (set ALIENFX_ALLOW_ANY_VENDOR=1 to override)\n";
	return false;
}

void PrintDryRun(const char* callName, const uint8_t* data, unsigned length) {
	std::ostream& os = Sink();
	os << "[dry-run] " << callName << " len=" << length << " " << std::hex << std::setfill('0');
	for (unsigned i = 0; i < length; ++i)
		os << std::setw(2) << static_cast<unsigned>(data[i]) << (i + 1 < length ? " " : "");
	os << std::dec << "\n";
	if (length > 0) {
		os << "  decode: report_id=0x" << std::hex << std::setw(2) << std::setfill('0')
		   << static_cast<unsigned>(data[0]);
		if (length > 1)
			os << " byte1=0x" << std::setw(2) << static_cast<unsigned>(data[1]);
		if (length > 2)
			os << " byte2=0x" << std::setw(2) << static_cast<unsigned>(data[2]);
		// V4's COMMV4_control layout (Doc/linux_roadmap/04-alienfx-sdk-hid.md, "V4
		// (34 bytes...)") puts the control type at offset 4 and the control ID
		// (little-endian) at offsets 5-6 -- the one version-specific decode worth
		// special-casing here, since M2d's live V4 hardware is this fork's only
		// validated device. Byte 2 (0x21, COMMV4_control's own opcode) must also
		// match: byte 1 (0x03) alone is the shared V4 "group" marker every V4
		// command uses, not specific to COMMV4_control -- checking only that byte
		// mislabels e.g. COMMV4_setOneColor's rgb/count payload as a control
		// type/ID.
		if (length >= 7 && data[0] == 0 && data[1] == 0x03 && data[2] == 0x21)
			os << " v4_control_type=0x" << std::setw(2) << static_cast<unsigned>(data[4])
			   << " v4_control_id=0x" << std::setw(4)
			   << (static_cast<unsigned>(data[5]) | (static_cast<unsigned>(data[6]) << 8));
		os << std::dec << "\n";
	}
}

} // namespace

namespace alienfx_hid {

bool IsKnownVendor(uint16_t vid) {
	switch (vid) {
	case 0x187c: case 0x0d62: case 0x0424: case 0x0461: case 0x04f2:
		return true;
	default:
		return false;
	}
}

void RegisterDevice(HANDLE handle, uint16_t vid, uint16_t pid) {
	std::lock_guard<std::mutex> lock(g_deviceMutex);
	g_devices[handle] = {vid, pid};
}

void ForgetDevice(HANDLE handle) {
	std::lock_guard<std::mutex> lock(g_deviceMutex);
	g_devices.erase(handle);
}

void SetDryRun(bool enabled) { g_dryRun = enabled; }
bool IsDryRun() { SeedFromEnvironmentOnce(); return g_dryRun; }

void SetDryRunSink(std::ostream* sink) {
	std::lock_guard<std::mutex> lock(g_sinkMutex);
	g_sink = sink ? sink : &std::cout;
}

void SetAllowAnyVendor(bool enabled) { g_allowAnyVendor = enabled; }
bool IsAllowAnyVendor() { SeedFromEnvironmentOnce(); return g_allowAnyVendor; }

void SetOutputMode(OutputMode mode) { g_outputMode = mode; }
OutputMode GetOutputMode() { SeedFromEnvironmentOnce(); return g_outputMode; }

void SetSleepObserver(SleepObserver observer) { g_sleepObserver = observer; }

} // namespace alienfx_hid

// --- hid_backend.h's free-function seam, implemented over hidapi. -------------
// Declared with no enclosing namespace in hid_backend.h, resolved from inside
// `namespace AlienFX_SDK` by ordinary (non-ADL) unqualified lookup walking out to
// the global namespace -- the same mechanism M2a's fake_hid.cpp relies on.
// Every wrapper reaches SeedFromEnvironmentOnce() indirectly via IsDryRun()/
// GetOutputMode()/IsAllowAnyVendor(), so ALIENFX_DRY_RUN etc. take effect from the
// first HID call in a process, however it was reached.

BOOL HidD_SetOutputReport(HANDLE device, void* reportBuffer, DWORD reportBufferLength) {
	auto* data = static_cast<uint8_t*>(reportBuffer);
	if (alienfx_hid::IsDryRun()) {
		PrintDryRun("HidD_SetOutputReport", data, reportBufferLength);
		return TRUE;
	}
	if (!CheckAllowlist(device, "HidD_SetOutputReport"))
		return FALSE;
	auto* dev = static_cast<hid_device*>(device);
	int n;
#if HID_API_VERSION >= HID_API_MAKE_VERSION(0, 15, 0)
	if (alienfx_hid::GetOutputMode() == alienfx_hid::OutputMode::Report)
		n = hid_send_output_report(dev, data, reportBufferLength);
	else
		n = hid_write(dev, data, reportBufferLength);
#else
	n = hid_write(dev, data, reportBufferLength);
#endif
	return n >= 0 ? TRUE : FALSE;
}

BOOL HidD_SetFeature(HANDLE device, void* reportBuffer, DWORD reportBufferLength) {
	auto* data = static_cast<uint8_t*>(reportBuffer);
	if (alienfx_hid::IsDryRun()) {
		PrintDryRun("HidD_SetFeature", data, reportBufferLength);
		return TRUE;
	}
	if (!CheckAllowlist(device, "HidD_SetFeature"))
		return FALSE;
	int n = hid_send_feature_report(static_cast<hid_device*>(device), data, reportBufferLength);
	return n >= 0 ? TRUE : FALSE;
}

BOOL HidD_GetFeature(HANDLE device, void* reportBuffer, DWORD reportBufferLength) {
	auto* data = static_cast<uint8_t*>(reportBuffer);
	if (alienfx_hid::IsDryRun()) {
		std::memset(data, 0, reportBufferLength);
		return TRUE;
	}
	// No allowlist gate: this is a read, not a write -- see CheckAllowlist's
	// comment for which three calls are gated and why.
	int n = hid_get_feature_report(static_cast<hid_device*>(device), data, reportBufferLength);
	return n >= 0 ? TRUE : FALSE;
}

BOOL HidD_GetInputReport(HANDLE device, void* reportBuffer, DWORD reportBufferLength) {
	auto* data = static_cast<uint8_t*>(reportBuffer);
	if (alienfx_hid::IsDryRun()) {
		std::memset(data, 0, reportBufferLength);
		return TRUE;
	}
	int n = hid_get_input_report(static_cast<hid_device*>(device), data, reportBufferLength);
	return n >= 0 ? TRUE : FALSE;
}

BOOL WriteFile(HANDLE file, const void* buffer, DWORD numberOfBytesToWrite,
               DWORD* numberOfBytesWritten, void* /*overlapped*/) {
	const auto* data = static_cast<const uint8_t*>(buffer);
	if (alienfx_hid::IsDryRun()) {
		PrintDryRun("WriteFile", data, numberOfBytesToWrite);
		if (numberOfBytesWritten) *numberOfBytesWritten = numberOfBytesToWrite;
		return TRUE;
	}
	if (!CheckAllowlist(file, "WriteFile"))
		return FALSE;
	int n = hid_write(static_cast<hid_device*>(file), data, numberOfBytesToWrite);
	if (n < 0)
		return FALSE;
	if (numberOfBytesWritten) *numberOfBytesWritten = static_cast<DWORD>(n);
	return TRUE;
}

BOOL ReadFile(HANDLE file, void* buffer, DWORD numberOfBytesToRead,
              DWORD* numberOfBytesRead, void* /*overlapped*/) {
	auto* data = static_cast<uint8_t*>(buffer);
	if (alienfx_hid::IsDryRun()) {
		std::memset(data, 0, numberOfBytesToRead);
		if (numberOfBytesRead) *numberOfBytesRead = 0;
		return TRUE;
	}
	// Defect 2: hid_read_timeout returns 0 on timeout, -1 on error. Windows'
	// ReadFile (with the COMMTIMEOUTS this SDK sets) returns TRUE with 0 bytes on
	// timeout -- V7's mandatory read-after-write (AlienFX_SDK.cpp:192-194) relies
	// on this succeeding even though its result is unused by the caller. Only -1
	// is a real failure.
	int n = hid_read_timeout(static_cast<hid_device*>(file), data, numberOfBytesToRead,
	                          kReadTimeoutMs);
	if (n < 0)
		return FALSE;
	if (numberOfBytesRead) *numberOfBytesRead = static_cast<DWORD>(n);
	return TRUE;
}

BOOL CloseHandle(HANDLE object) {
	if (!alienfx_hid::IsDryRun() && object)
		hid_close(static_cast<hid_device*>(object));
	alienfx_hid::ForgetDevice(object);
	return TRUE;
}

void Sleep(DWORD milliseconds) {
	if (g_sleepObserver) g_sleepObserver(milliseconds);
	std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}
