#include "fake_hidapi.h"

#include "hid_backend_linux.h"

namespace alienfx_test {
namespace {

void SleepTrampoline(unsigned ms) {
	GetFakeHidapi().RecordSleep(ms);
}

} // namespace

void FakeHidapiTransport::Reset() {
	log_.clear();
	readQueue_.clear();
	deviceInfo_.clear();
	lastRequestedReportId_ = 0;
}

void FakeHidapiTransport::QueueRead(std::vector<uint8_t> response) {
	readQueue_.push_back(std::move(response));
}

void FakeHidapiTransport::SetDeviceInfo(hid_device* dev, uint16_t vid, uint16_t pid) {
	hid_device_info info{};
	info.vendor_id = vid;
	info.product_id = pid;
	info.interface_number = -1;
	info.bus_type = HID_API_BUS_USB;
	deviceInfo_[dev] = info;
}

int FakeHidapiTransport::RecordWrite(TransportKind kind, const uint8_t* buffer, size_t length) {
	log_.push_back({kind, std::vector<uint8_t>(buffer, buffer + length), 0});
	return static_cast<int>(length);
}

int FakeHidapiTransport::ServiceRead(TransportKind kind, uint8_t* buffer, size_t length) {
	std::vector<uint8_t> response;
	if (!readQueue_.empty()) {
		response = std::move(readQueue_.front());
		readQueue_.pop_front();
		response.resize(length, 0);
	} else {
		// Same "always ready" default as fake_hid.h's FakeHidTransport::ServiceRead
		// -- see that file's comment for why both status conventions are set at once.
		response.assign(length, 0);
		if (length > 0) response[0] = 0x10;
		if (length > 2) response[2] = 33;
	}
	std::copy(response.begin(), response.end(), buffer);
	log_.push_back({kind, response, 0});
	return static_cast<int>(length);
}

int FakeHidapiTransport::SendOutputReport(hid_device*, const uint8_t* data, size_t length) {
	return RecordWrite(TransportKind::Out, data, length);
}
int FakeHidapiTransport::SendFeatureReport(hid_device*, const uint8_t* data, size_t length) {
	return RecordWrite(TransportKind::Feat, data, length);
}
int FakeHidapiTransport::Write(hid_device*, const uint8_t* data, size_t length) {
	return RecordWrite(TransportKind::Write, data, length);
}
int FakeHidapiTransport::ReadTimeout(hid_device*, uint8_t* data, size_t length, int /*milliseconds*/) {
	return ServiceRead(TransportKind::Read, data, length);
}
int FakeHidapiTransport::GetFeatureReport(hid_device*, uint8_t* data, size_t length) {
	if (length > 0) lastRequestedReportId_ = data[0];
	return ServiceRead(TransportKind::GetFeat, data, length);
}
int FakeHidapiTransport::GetInputReport(hid_device*, uint8_t* data, size_t length) {
	if (length > 0) lastRequestedReportId_ = data[0];
	return ServiceRead(TransportKind::GetIn, data, length);
}
void FakeHidapiTransport::Close(hid_device* dev) {
	// Not logged: teardown (~Functions -> hid_backend_linux.cpp's CloseHandle),
	// not part of the packet sequence any golden vector asserts against -- same
	// convention fake_hid.h's CloseHandle wrapper uses.
	deviceInfo_.erase(dev);
}
const hid_device_info* FakeHidapiTransport::DeviceInfo(hid_device* dev) {
	auto it = deviceInfo_.find(dev);
	return it == deviceInfo_.end() ? nullptr : &it->second;
}
void FakeHidapiTransport::RecordSleep(unsigned ms) {
	log_.push_back({TransportKind::Sleep, {}, ms});
}

FakeHidapiTransport& GetFakeHidapi() {
	static FakeHidapiTransport instance;
	// Installed once, on first use of the fake -- hid_backend_linux.cpp's Sleep()
	// has no hidapi call to intercept, so it calls this hook directly (see
	// hid_backend_linux.h's SleepObserver) in addition to actually sleeping.
	static bool installed = [] {
		alienfx_hid::SetSleepObserver(&SleepTrampoline);
		return true;
	}();
	(void)installed;
	return instance;
}

} // namespace alienfx_test

// --- hidapi's own C API, implemented over the fake instance. -----------------
// Only the entry points hid_backend_linux.cpp actually calls -- this is not a
// full hidapi stub, and deliberately doesn't implement hid_init/hid_exit/
// hid_enumerate/hid_open/hid_open_path, since M2b never calls those (device
// opening is M2c's).

int hid_send_output_report(hid_device* dev, const unsigned char* data, size_t length) {
	return alienfx_test::GetFakeHidapi().SendOutputReport(dev, data, length);
}
int hid_send_feature_report(hid_device* dev, const unsigned char* data, size_t length) {
	return alienfx_test::GetFakeHidapi().SendFeatureReport(dev, data, length);
}
int hid_write(hid_device* dev, const unsigned char* data, size_t length) {
	return alienfx_test::GetFakeHidapi().Write(dev, data, length);
}
int hid_read_timeout(hid_device* dev, unsigned char* data, size_t length, int milliseconds) {
	return alienfx_test::GetFakeHidapi().ReadTimeout(dev, data, length, milliseconds);
}
int hid_get_feature_report(hid_device* dev, unsigned char* data, size_t length) {
	return alienfx_test::GetFakeHidapi().GetFeatureReport(dev, data, length);
}
int hid_get_input_report(hid_device* dev, unsigned char* data, size_t length) {
	return alienfx_test::GetFakeHidapi().GetInputReport(dev, data, length);
}
void hid_close(hid_device* dev) {
	alienfx_test::GetFakeHidapi().Close(dev);
}
struct hid_device_info* hid_get_device_info(hid_device* dev) {
	// hid_get_device_info() returns a const-correct-by-convention pointer the
	// caller must not free (see hidapi.h's doc comment); hid_backend_linux.cpp
	// only reads vendor_id/product_id from it, so the const_cast here is no
	// worse than what the real library itself does for the same signature.
	return const_cast<hid_device_info*>(alienfx_test::GetFakeHidapi().DeviceInfo(dev));
}
const wchar_t* hid_error(hid_device*) {
	return nullptr;
}
