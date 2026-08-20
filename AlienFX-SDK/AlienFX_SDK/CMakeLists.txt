# AlienFX-SDK/AlienFX_SDK -- the HID light SDK.
#
# M1 scope: header-only. AlienFX_SDK.h and alienfx-controls.h compile against
# Common/win_compat.h, with the namespace-scoped `byte` typedef and the layout of
# the on-wire union types pinned down by tests/alienfx_sdk/sdk_headers_test.cpp.
# alienfx_sdk_headers is INTERFACE for exactly that reason -- there is no .cpp to
# compile yet. M2 turns this same file into a real STATIC library
# (add_library(alienfx_sdk STATIC AlienFX_SDK.cpp ...)) once AlienFX_SDK.cpp
# itself is ported; do not invent a second CMakeLists.txt for that -- extend this
# one, per Doc/linux_roadmap/02-build-system.md's per-target-file convention.
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
