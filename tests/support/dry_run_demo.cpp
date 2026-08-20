// Hand-run demo of Doc/linux_roadmap/16-testing-and-validation.md's `--dry-run`
// transport (not wired into ctest, same convention as gen_golden.cpp). Forces
// alienfx_hid::SetDryRun(true) before doing anything else, so this never opens or
// writes to a real device regardless of how it's invoked or linked -- see
// tests/CMakeLists.txt's comment on why it's still linked against the real
// hidapi::hidraw rather than the fake.
//
// Prints AlienFX-SDK/AlienFX_SDK/hid_backend_linux.cpp's decoded form for a
// Reset -> SetColor -> UpdateColors sequence on a fake API_V4 device (this
// fork's one validated device class, per Doc/linux_roadmap/local/test-machine.md).
//
// Usage: dry_run_demo

#include "AlienFX_SDK.h"
#include "hid_backend_linux.h"

#include <cstdio>

int main() {
	alienfx_hid::SetDryRun(true);

#ifndef ALIENFX_TESTING
	std::fprintf(stderr, "dry_run_demo needs ALIENFX_TESTING (see tests/CMakeLists.txt)\n");
	return 1;
#else
	AlienFX_SDK::Functions f;
	f.version = AlienFX_SDK::API_V4;
	f.TestSetDeviceState(reinterpret_cast<HANDLE>(0x1), 34, 0x187c, 0x0550);

	std::printf("--- Reset ---\n");
	f.Reset();
	std::printf("--- SetColor(light 0, red) ---\n");
	f.SetColor(0, AlienFX_SDK::Afx_action{
		(BYTE)AlienFX_SDK::AlienFX_A_Color, 0, 0, 0xff, 0, 0});
	std::printf("--- UpdateColors ---\n");
	f.UpdateColors();
	return 0;
#endif
}
