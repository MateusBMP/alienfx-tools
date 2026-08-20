#pragma once
// Linux-only. On Windows this codebase uses the real Win32 HID API (<hidsdi.h>) and
// kernel32 (WriteFile/ReadFile/CloseHandle/Sleep) directly -- this header exists so
// AlienFX_SDK.cpp's `Functions` class (the HID protocol state machine, see
// Doc/linux_roadmap/04-alienfx-sdk-hid.md's "Functions vs. Mappings" split) has
// something to link against on Linux without committing to a transport yet.
//
// Two implementations of these exact symbols exist:
//   - tests/support/fake_hid.cpp (M2a): records every call instead of touching a
//     device. Used to freeze golden vectors and drive
//     tests/alienfx_sdk/protocol_invariants_test.cpp with no hardware and no
//     hidapi dependency.
//   - hid_backend_linux.cpp (M2b, not yet written): the real implementation over
//     hidapi-hidraw, per doc 04's "hidraw / hidapi mapping" table.
//
// Exactly one of the two is linked into any given binary. Signatures match the
// existing call sites in AlienFX_SDK.cpp's PrepareAndSend / GetDeviceStatus /
// WaitForReady / WaitForBusy / ~Functions verbatim (see doc 04, "The transport
// core"), so no call site changes between platforms or between M2a and M2b.
// Device enumeration (AlienFXProbeDevice/AlienFXInitialize) and SetupAPI are a
// separate concern -- deferred to M2c -- and declare nothing here.

#ifdef _WIN32
#error "hid_backend.h is Linux-only; the Windows build uses <hidsdi.h> directly"
#endif

#include "win_compat.h"

BOOL HidD_SetOutputReport(HANDLE device, void* reportBuffer, DWORD reportBufferLength);
BOOL HidD_SetFeature(HANDLE device, void* reportBuffer, DWORD reportBufferLength);
BOOL HidD_GetFeature(HANDLE device, void* reportBuffer, DWORD reportBufferLength);
BOOL HidD_GetInputReport(HANDLE device, void* reportBuffer, DWORD reportBufferLength);

BOOL WriteFile(HANDLE file, const void* buffer, DWORD numberOfBytesToWrite,
               DWORD* numberOfBytesWritten, void* overlapped);
BOOL ReadFile(HANDLE file, void* buffer, DWORD numberOfBytesToRead,
              DWORD* numberOfBytesRead, void* overlapped);

BOOL CloseHandle(HANDLE object);
void Sleep(DWORD milliseconds);
