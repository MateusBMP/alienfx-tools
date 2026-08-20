cmake_minimum_required(VERSION 3.24...4.0)

# -----------------------------------------------------------------------------------
# This CMake build is the LINUX build only. alienfx-tools.sln remains the canonical
# Windows build (toolset v145) and is never driven from here.
#   Doc/linux_roadmap/02-build-system.md, "What NOT to do"
# -----------------------------------------------------------------------------------
if(WIN32 AND NOT ALIENFX_ALLOW_WINDOWS_CMAKE)
    message(FATAL_ERROR
        "This CMake build targets Linux. On Windows, use alienfx-tools.sln.\n"
        "Override with -DALIENFX_ALLOW_WINDOWS_CMAKE=ON only if you know why.")
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not supported (object files would otherwise land in "
        "upstream-owned directories). Use: cmake -S . -B build")
endif()

# PROJECT_VERSION mirrors PRODUCTVERSION in LightFX/LightFX.rc:65, which is where the
# Windows build reads its version from at runtime (Common/Common.cpp GetAppVersion()).
# There is no .rc file on Linux, so this is the Linux source of truth; keep it in sync
# by hand. Do NOT try to unify the two version sources from the Linux side -- the tree
# is already internally inconsistent about this (e.g. alienfx-mon/alienfx-mon.rc has a
# different FILEVERSION than PRODUCTVERSION), and reconciling that would be a Windows
# build change, which is out of scope for Linux porting work per CLAUDE.md.
project(alienfx-tools
    VERSION      9.6.1.2
    DESCRIPTION  "Alienware/Dell light, fan and power control"
    HOMEPAGE_URL "https://github.com/T-Troll/alienfx-tools"
    LANGUAGES    C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# gnu++17, not c++17, would make GCC and Clang agree on more MS-extension tolerance
# than they do on real distros -- turning it off is what makes the GCC/Clang pair of
# presets in CMakePresets.json an actually useful check (Doc/linux_roadmap/16).
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)     # clangd / IDE tooling
set(CMAKE_POSITION_INDEPENDENT_CODE ON)   # liblightfx.so (M9) needs PIC dependencies

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# --- Options -------------------------------------------------------------------
# The first four names/defaults are fixed by Doc/linux_roadmap/02-build-system.md --
# do not rename them without updating that doc.
option(ALIENFX_BUILD_CLI    "Build alienfx-cli / alienfan-cli"        ON)
option(ALIENFX_BUILD_DAEMON "Build the background daemon"             ON)
option(ALIENFX_BUILD_GUI    "Build the Qt6 GUI"                       OFF)
option(ALIENFX_BUILD_TESTS  "Build unit tests"                        OFF)
# Fork-added (not in doc 02): treat warnings as errors on the alienfx::warnings
# INTERFACE target. Off by default so a plain `cmake --build` never surprises a
# contributor; the gcc/clang CMakePresets.json presets turn it on explicitly.
option(ALIENFX_WERROR       "Treat compiler warnings as errors"       OFF)

include(AlienfxWarnings)     # -> alienfx::warnings (INTERFACE)
include(AlienfxDependency)   # -> alienfx_require_package()

# --- Targets ---------------------------------------------------------------------
# kiss_fft is vendored third-party DSP code that happens to live under alienfx-gui/.
# It is deliberately NOT gated behind ALIENFX_BUILD_GUI: it has no Windows and no GUI
# dependency, and gating it here would make a default `cmake --build` compile nothing
# at all, which defeats M0's whole purpose. It is a leaf dependency consumed by the
# haptics code (M8) and by the test suite below.
add_subdirectory(alienfx-gui/kiss_fft)

add_subdirectory(Common)                          # alienfx::compat, alienfx::common
add_subdirectory(AlienFX-SDK/AlienFX_SDK)         # alienfx::sdk_headers (M1: headers
                                                   # only; M2 adds the STATIC library)
# M4 : add_subdirectory(RegHelperLib)             # reg_helper
# M5 : add_subdirectory(...)                      # alienfan_sdk (new Linux implementation)
# M6 : add_subdirectory(...)                      # alienfxd (new daemon)
# M9 : add_subdirectory(LightFX)                  # lightfx

if(ALIENFX_BUILD_TESTS)
    # enable_testing(), not include(CTest): the latter adds a competing BUILD_TESTING
    # option plus CDash machinery this project has no use for. ALIENFX_BUILD_TESTS
    # stays the single switch.
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "alienfx-tools ${PROJECT_VERSION} (Linux/CMake)")
message(STATUS "  ALIENFX_BUILD_CLI=${ALIENFX_BUILD_CLI} "
               "ALIENFX_BUILD_DAEMON=${ALIENFX_BUILD_DAEMON} "
               "ALIENFX_BUILD_GUI=${ALIENFX_BUILD_GUI} "
               "ALIENFX_BUILD_TESTS=${ALIENFX_BUILD_TESTS}")
