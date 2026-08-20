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

# M2+: add_executable(alienfx_sdk_tests alienfx_sdk/packet_builder_test.cpp ...)
#      reading golden vectors from tests/golden/alienfx_sdk/ -- see tests/README.md.
