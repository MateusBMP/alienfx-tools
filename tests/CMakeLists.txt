# Test suite for the Linux port. See tests/README.md for conventions.
#
# GoogleTest over Catch2/doctest: gtest_discover_tests() (included(GoogleTest) below)
# lives in CMake's OWN builtin GoogleTest.cmake module, so test discovery works
# identically whether GTest came from the system package or from FetchContent.
# Catch2's equivalent ships inside the Catch2 source tree itself, which would need
# mode-dependent CMAKE_MODULE_PATH handling -- exactly what alienfx_require_package()
# exists to avoid. CMake also ships FindGTest.cmake in the box (exporting GTest::gtest,
# GTest::gtest_main, GTest::gmock, GTest::gmock_main), so find_package(GTest) succeeds
# even on a distro whose package doesn't ship its own CMake config.
alienfx_require_package(googletest
    FIND_ARGS      NAMES GTest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.17.0
    OPTIONS        INSTALL_GTEST=OFF
                   BUILD_GMOCK=ON
                   gtest_force_shared_crt=ON)

include(GoogleTest)   # builtin CMake module: gtest_discover_tests(), works the same
                       # in both the system-package and the FetchContent acquisition path.

# --- kiss_fft ------------------------------------------------------------------
add_executable(kiss_fft_tests kiss_fft/kiss_fft_test.cpp)
target_link_libraries(kiss_fft_tests PRIVATE
    alienfx::kiss_fft
    alienfx::warnings
    GTest::gmock
    GTest::gtest_main)
gtest_discover_tests(kiss_fft_tests)

# --- compat (M1) -------------------------------------------------------------
add_executable(compat_tests compat/win_compat_test.cpp)
target_link_libraries(compat_tests PRIVATE
    alienfx::compat
    alienfx::warnings
    GTest::gmock
    GTest::gtest_main)
gtest_discover_tests(compat_tests)

# --- common (M1) -------------------------------------------------------------
add_executable(common_tests
    common/custom_mutex_test.cpp
    common/thread_helper_test.cpp)
target_link_libraries(common_tests PRIVATE
    alienfx::common
    alienfx::warnings
    GTest::gmock
    GTest::gtest_main)
gtest_discover_tests(common_tests)

# --- alienfx_sdk (M1: headers only; M2 adds packet-builder sources here) -------
add_executable(alienfx_sdk_tests alienfx_sdk/sdk_headers_test.cpp)
target_link_libraries(alienfx_sdk_tests PRIVATE
    alienfx::sdk_headers
    alienfx::warnings
    GTest::gmock
    GTest::gtest_main)
gtest_discover_tests(alienfx_sdk_tests)

# --- alienfx test support (M2a/M2b) ----------------------------------------------
# The parts every fake transport and both packet-test executables share:
# golden_vector.{h,cpp} reads/writes the extended hex format golden vectors are
# stored in; transport_log.{h,cpp} is the TransportKind/TransportEvent vocabulary
# both fake_hid.cpp (below) and fake_hidapi.cpp record into, so a golden file
# means the same thing under either fake; packet_matrix.{h,cpp} is the shared
# version x operation table gen_golden, alienfx_sdk_packet_tests, and
# alienfx_sdk_transport_tests all replay, so all three are provably driving
# identical calls. ALIENFX_TESTING is PUBLIC so it reaches AlienFX-SDK/AlienFX_SDK/
# AlienFX_SDK.h's TestSetDeviceState seam in every consumer -- never defined
# outside this test tree, so it never reaches a shipped binary on either platform.
add_library(alienfx_test_support STATIC
    support/transport_log.cpp
    support/golden_vector.cpp
    support/packet_matrix.cpp)
target_include_directories(alienfx_test_support PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/support>)
target_compile_definitions(alienfx_test_support PUBLIC ALIENFX_TESTING)
target_link_libraries(alienfx_test_support
    PUBLIC  alienfx::sdk
    PRIVATE alienfx::warnings)

# --- alienfx_test_fake_hid (M2a) -------------------------------------------------
# fake_hid.cpp implements hid_backend.h's Windows-shaped transport seam directly
# (HidD_SetOutputReport/WriteFile/ReadFile/...) by recording every call instead of
# touching a device (Doc/linux_roadmap/16-testing-and-validation.md, "The
# recording fake transport"). Split from alienfx_test_support (M2b) once a second
# fake (fake_hidapi, below) needed the shared matrix/golden-vector plumbing
# without also linking this one -- the two fakes back different transport seams
# and are never linked into the same binary.
add_library(alienfx_test_fake_hid STATIC support/fake_hid.cpp)
target_link_libraries(alienfx_test_fake_hid
    PUBLIC  alienfx_test_support
    PRIVATE alienfx::warnings)

# --- alienfx_test_fake_hidapi (M2b) ----------------------------------------------
# fake_hidapi.cpp implements the *hidapi* C API instead -- the seam
# hid_backend_linux.cpp (alienfx::hid_linux) actually calls. Links hidapi::include
# for the header-only hid_device/hid_device_info declarations it stubs, never
# hidapi::hidraw -- see AlienFX-SDK/AlienFX_SDK/CMakeLists.txt's comment on
# alienfx_hid_linux for why exactly one hidapi implementation may ever be linked
# into a given binary.
add_library(alienfx_test_fake_hidapi STATIC support/fake_hidapi.cpp)
target_link_libraries(alienfx_test_fake_hidapi
    PUBLIC  alienfx_test_support
            hidapi::include   # transport_backend_test.cpp also #includes <hidapi.h> directly
    PRIVATE alienfx::warnings
            alienfx::hid_linux)

# --- alienfx_sdk_packet_tests (M2a) ---------------------------------------------
# A separate executable from alienfx_sdk_tests above (which stays header-only, M1
# scope): this one links the compiled alienfx::sdk library and the fake transport,
# a materially different dependency profile. Golden vectors are read via an
# absolute source-tree path -- ctest's working directory isn't guaranteed to be
# the source tree, so a relative path would be fragile across CMake/CTest
# versions and generators.
add_executable(alienfx_sdk_packet_tests
    alienfx_sdk/packet_builder_test.cpp
    alienfx_sdk/protocol_invariants_test.cpp)
target_compile_definitions(alienfx_sdk_packet_tests PRIVATE
    ALIENFX_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden/alienfx_sdk")
target_link_libraries(alienfx_sdk_packet_tests PRIVATE
    alienfx_test_fake_hid
    alienfx::warnings
    GTest::gmock
    GTest::gtest_main)
gtest_discover_tests(alienfx_sdk_packet_tests)

# --- alienfx_sdk_transport_tests (M2b) -------------------------------------------
# Replays the SAME packet_matrix against alienfx::hid_linux (the real backend)
# linked against fake_hidapi instead of the real hidapi implementation, asserting
# byte-identical output against the SAME golden files alienfx_sdk_packet_tests
# checks above -- the fake-hidapi tier of Doc/linux_roadmap/
# 16-testing-and-validation.md, proving hid_backend_linux.cpp's hidapi call
# mapping without hardware, root, or a real hidraw node. Deliberately does not
# link hidapi::hidraw anywhere in this executable's dependency graph -- verified
# by `ldd` in Doc/linux_roadmap/17-milestones.md's M2b verification steps.
add_executable(alienfx_sdk_transport_tests
    alienfx_sdk/transport_backend_test.cpp)
target_compile_definitions(alienfx_sdk_transport_tests PRIVATE
    ALIENFX_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden/alienfx_sdk")
target_link_libraries(alienfx_sdk_transport_tests PRIVATE
    alienfx_test_fake_hidapi
    alienfx::hid_linux
    alienfx::warnings
    GTest::gmock
    GTest::gtest_main)
gtest_discover_tests(alienfx_sdk_transport_tests)

# --- gen_golden (M2a) ------------------------------------------------------------
# The golden-vector generator. Run by hand (see tests/README.md) -- deliberately
# NOT registered with gtest_discover_tests/ctest, since golden vectors are
# committed artifacts, not something a test run should regenerate.
add_executable(gen_golden support/gen_golden.cpp)
target_link_libraries(gen_golden PRIVATE
    alienfx_test_fake_hid
    alienfx::warnings)

# --- dry_run_demo (M2b) -----------------------------------------------------------
# Hand-run tool (not wired into ctest, same convention as gen_golden above)
# demonstrating Doc/linux_roadmap/16-testing-and-validation.md's `--dry-run`
# transport: forces alienfx_hid::SetDryRun(true) in main() before sending
# anything, so it never opens or writes to a device regardless of how it's
# linked. Links the real alienfx::hid_linux + hidapi::hidraw (not the fake) so it
# doubles as a real diagnostic tool once M2c's device-open path exists --
# dry-run being forced on is what keeps it safe today.
add_executable(dry_run_demo support/dry_run_demo.cpp)
target_compile_definitions(dry_run_demo PRIVATE ALIENFX_TESTING) # for TestSetDeviceState
target_link_libraries(dry_run_demo PRIVATE
    alienfx::sdk
    alienfx::hid_linux
    hidapi::hidraw
    alienfx::warnings)
