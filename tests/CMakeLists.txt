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

# --- alienfx test support (M2a) -------------------------------------------------
# fake_hid.cpp implements hid_backend.h's transport seam by recording every call
# instead of touching a device (Doc/linux_roadmap/16-testing-and-validation.md,
# "The recording fake transport"); golden_vector.{h,cpp} reads/writes the extended
# hex format it's stored in; packet_matrix.{h,cpp} is the shared version x
# operation table both gen_golden and alienfx_sdk_packet_tests replay, so the two
# are provably driving identical calls. ALIENFX_TESTING is PUBLIC so it reaches
# AlienFX-SDK/AlienFX_SDK/AlienFX_SDK.h's TestSetDeviceState seam in every
# consumer -- never defined outside this test tree, so it never reaches a shipped
# binary on either platform.
add_library(alienfx_test_support STATIC
    support/fake_hid.cpp
    support/golden_vector.cpp
    support/packet_matrix.cpp)
target_include_directories(alienfx_test_support PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/support>)
target_compile_definitions(alienfx_test_support PUBLIC ALIENFX_TESTING)
target_link_libraries(alienfx_test_support
    PUBLIC  alienfx::sdk
    PRIVATE alienfx::warnings)

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
    alienfx_test_support
    alienfx::warnings
    GTest::gmock
    GTest::gtest_main)
gtest_discover_tests(alienfx_sdk_packet_tests)

# --- gen_golden (M2a) ------------------------------------------------------------
# The golden-vector generator. Run by hand (see tests/README.md) -- deliberately
# NOT registered with gtest_discover_tests/ctest, since golden vectors are
# committed artifacts, not something a test run should regenerate.
add_executable(gen_golden support/gen_golden.cpp)
target_link_libraries(gen_golden PRIVATE
    alienfx_test_support
    alienfx::warnings)
