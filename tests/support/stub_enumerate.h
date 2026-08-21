#pragma once
// M2c's scripted stub for hid_enumerate.h's seam (AlienFX-SDK/AlienFX_SDK/
// hid_enumerate.h) -- the enumeration-side counterpart to fake_hid.h/fake_hidapi.h,
// which back the transport-side seam instead. Contains no hidapi calls at all: it is
// a plain in-memory fake, so linking it never introduces a hidapi dependency (matches
// fake_hid.h's own "no hidapi" property, one layer further down the stack than
// fake_hidapi.h's).
//
// Unlike fake_hid.h/fake_hidapi.h (one fake each, chosen per test tier), this is the
// ONLY enumeration-seam provider a test binary ever needs: alienfx::sdk requires this
// seam unconditionally now that AlienFX_SDK.cpp's Linux AlienFXProbeDevice/
// AlienFXInitialize call it (see tests/CMakeLists.txt's alienfx_test_enum_stub
// target). In its default, unscripted state -- EnumerateNodes returns no candidates,
// OpenNode always fails -- every existing M2a/M2b test binary that links it
// (alienfx_sdk_packet_tests, alienfx_sdk_transport_tests, gen_golden, dry_run_demo)
// keeps its exact "structurally cannot open a device" property unchanged: nothing in
// any of those binaries calls SetEnumNodes/SetOpenNodeSucceeds, so the stub never
// leaves its default state for them. Only tests/alienfx_sdk/detection_test.cpp
// scripts it.

#include <cstdint>
#include <string>
#include <vector>

#include "hid_enumerate.h"

namespace alienfx_test {

// Candidates alienfx_hid::EnumerateNodes returns on its next call (and every call
// after, until changed again) -- default empty. Does not affect OpenNode.
void SetEnumNodes(std::vector<alienfx_hid::HidNode> nodes);

// Whether alienfx_hid::OpenNode succeeds at all -- default false, matching the "OpenNode
// always fails" default every non-detection binary relies on. When true, OpenNode
// returns a fresh, distinct non-null HANDLE for any node whose path is not in the
// unopenable set below, and records {vid, pid} as the most recent successful
// "registration" (this stub's own record, standing in for the real
// alienfx_hid::RegisterDevice a real backend would call -- see hid_enumerate.h;
// nothing here touches the real one, so linking this stub never requires
// hid_backend_linux.cpp).
void SetOpenNodeSucceeds(bool succeeds);

// Paths that fail to open even while SetOpenNodeSucceeds(true) is in effect -- for
// exercising AlienFXInitialize's "skip an unopenable candidate and keep going" path.
// Default empty.
void SetUnopenablePaths(std::vector<std::string> paths);

// Clears all of the above back to their defaults and clears the call/registration
// records below. Call between test cases, same convention fake_hid.h's Reset() uses.
void ResetEnumStub();

// --- Observability for detection_test.cpp; not used by production code. ---
unsigned OpenNodeSuccessCount();
uint16_t LastRegisteredVid();
uint16_t LastRegisteredPid();

} // namespace alienfx_test
