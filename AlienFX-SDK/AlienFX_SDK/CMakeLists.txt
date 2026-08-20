# AlienFX-SDK/AlienFX_SDK -- the HID light SDK.
#
# Two targets, per Doc/linux_roadmap/02-build-system.md's per-target-file
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
#   fake, see tests/CMakeLists.txt) today, hid_backend_linux.cpp (M2b's real
#   hidapi-hidraw backend) later. That's what lets M2a freeze golden vectors with
#   no hardware and no hidapi dependency.
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
