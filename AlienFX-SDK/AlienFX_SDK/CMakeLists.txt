# AlienFX-SDK/AlienFX_SDK -- the HID light SDK.
#
# Five targets, per Doc/linux_roadmap/02-build-system.md's per-target-file
# convention (extend this one file rather than adding a second CMakeLists.txt):
#
#   alienfx_sdk_headers (INTERFACE, M1) -- AlienFX_SDK.h and alienfx-controls.h,
#   pinned down by tests/alienfx_sdk/sdk_headers_test.cpp. No .cpp compiled here.
#
#   alienfx_sdk (STATIC, M2a) -- AlienFX_SDK.cpp's `Functions` class, the HID
#   protocol implementation. `Mappings` (registry-backed persistence) and SetupAPI
#   device enumeration are wrapped in #ifdef _WIN32 inside the .cpp and compile to
#   nothing here -- see Doc/linux_roadmap/04-alienfx-sdk-hid.md's "Functions vs.
#   Mappings" split. This target deliberately does NOT provide a transport backend:
#   hid_backend.h's HID/kernel32 symbols are left undefined by this library and
#   supplied by whoever links it -- tests/support/fake_hid.cpp (M2a's recording
#   fake, see tests/CMakeLists.txt) today, alienfx::hid_linux (M2b's real
#   hidapi-backed implementation of that same seam, below) for a real binary.
#
#   alienfx_hid_linux (STATIC, M2b) -- hid_backend_linux.cpp, the real
#   implementation over hidapi. Deliberately links hidapi::include (headers only),
#   never hidapi::hidraw itself -- the actual hidapi implementation is supplied by
#   whoever links the final binary, same "linker supplies the seam" convention as
#   alienfx_sdk above one level up: a shipped binary links the real
#   hidapi::hidraw, tests/alienfx_sdk/transport_backend_test.cpp links
#   tests/support/fake_hidapi.cpp instead. This is what lets M2b's golden-vector
#   replay run with no hardware, no root, and no real hidraw node -- see
#   Doc/linux_roadmap/17-milestones.md's M2b section for why that split exists.
add_library(alienfx_sdk_headers INTERFACE)
add_library(alienfx::sdk_headers ALIAS alienfx_sdk_headers)

target_include_directories(alienfx_sdk_headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

# NOACPILIGHTS is required, not a style choice: without it, AlienFX_SDK.h:7 pulls
# in alienfan-SDK.h, which itself #includes <wtypes.h> (its own line 5) -- and
# that SDK (v1, ACPI-backed) is explicitly not being ported to Linux, see
# Doc/linux_roadmap/05-alienfan-sdk-thermal.md. The ACPI light path
# (4 #ifndef NOACPILIGHTS blocks in AlienFX_SDK.cpp) stays Windows-only.
target_compile_definitions(alienfx_sdk_headers INTERFACE NOACPILIGHTS)

target_link_libraries(alienfx_sdk_headers INTERFACE alienfx::compat)

add_library(alienfx_sdk STATIC AlienFX_SDK.cpp)
add_library(alienfx::sdk ALIAS alienfx_sdk)

target_link_libraries(alienfx_sdk
    PUBLIC  alienfx::sdk_headers
    PRIVATE alienfx::warnings)

# hidapi is required at the top-level CMakeLists.txt, not here: find_package()'s
# IMPORTED targets (hidapi::include, hidapi::hidraw) are only visible in the
# directory that called it and that directory's own subdirectories -- calling it
# here would hide them from tests/, a sibling of this directory, not a child of
# it. Top level is the shallowest common ancestor of both.
add_library(alienfx_hid_linux STATIC hid_backend_linux.cpp)
add_library(alienfx::hid_linux ALIAS alienfx_hid_linux)

target_link_libraries(alienfx_hid_linux
    PUBLIC  alienfx::sdk_headers   # win_compat.h, HANDLE/BOOL/DWORD
    PRIVATE alienfx::warnings
            hidapi::include)       # headers only -- see the file comment above

#   alienfx_hid_descriptor (STATIC, M2c) -- hid_report_descriptor.cpp, the HID
#   report-descriptor parser Finding 1's fix depends on. Split into its own target,
#   deliberately NOT folded into alienfx_hid_enum_linux below, for the same reason
#   alienfx_hid_linux above is split off alienfx_sdk one level up: its test
#   (tests/alienfx_sdk/report_descriptor_test.cpp) needs zero hidapi in its
#   dependency graph, the same structural-cannot-open-a-device property M2a/M2b's
#   tests already have. No dependency beyond alienfx::warnings -- this parser takes
#   raw bytes in and a HidCaps out, nothing more.
add_library(alienfx_hid_descriptor STATIC hid_report_descriptor.cpp)
add_library(alienfx::hid_descriptor ALIAS alienfx_hid_descriptor)

# Its own include directory only -- deliberately NOT alienfx::sdk_headers (which
# would also pull in alienfx::compat/win_compat.h): this parser takes raw bytes in
# and a HidCaps out, nothing Windows-compat-typed, and report_descriptor_test.cpp's
# whole point is a dependency graph with nothing else in it.
target_include_directories(alienfx_hid_descriptor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(alienfx_hid_descriptor PRIVATE alienfx::warnings)

#   alienfx_hid_enum_linux (STATIC, M2c) -- hid_enumerate_linux.cpp, the real
#   implementation of hid_enumerate.h's seam (AlienFXProbeDevice/AlienFXInitialize's
#   device-open path, deferred from M2b to M2c -- see that file's header comment).
#   Same "linker supplies the seam" convention as alienfx_hid_linux above: links
#   hidapi::include (headers only, never hidapi::hidraw itself) plus
#   alienfx::hid_descriptor (the parser) and alienfx::hid_linux (for the shared
#   alienfx_hid::IsKnownVendor vendor allowlist -- declared once in
#   hid_backend_linux.h, not duplicated here).
add_library(alienfx_hid_enum_linux STATIC hid_enumerate_linux.cpp)
add_library(alienfx::hid_enum_linux ALIAS alienfx_hid_enum_linux)

target_link_libraries(alienfx_hid_enum_linux
    PUBLIC  alienfx::sdk_headers
    PRIVATE alienfx::warnings
            alienfx::hid_descriptor
            alienfx::hid_linux
            hidapi::include)
