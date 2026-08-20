# Common/ -- shared Win32 helpers, platform-abstracted per
# Doc/linux_roadmap/03-platform-abstraction.md.
#
# Two targets, not one, because Common/common.h + Common/Common.cpp are GUI code
# (13 of their 17 declarations take HWND or NOTIFYICONDATA*) and are deliberately
# NOT ported here -- doc 03 already says HWND must not be shimmed, and that file
# has its own unrelated case-mismatch problem ("Common.h" vs "common.h") to sort
# out when M7 gets to it. Only CustomMutex and ThreadHelper -- the two files M1's
# exit criteria actually name -- are built on Linux right now.

# --- alienfx::compat -----------------------------------------------------------
# INTERFACE only: win_compat.h is a header, nothing to compile. Split out from
# alienfx_common below so AlienFX-SDK/AlienFX_SDK can depend on just the header
# without pulling in the mutex/thread objects it doesn't need.
add_library(alienfx_compat INTERFACE)
add_library(alienfx::compat ALIAS alienfx_compat)
target_include_directories(alienfx_compat INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

# --- alienfx::common -------------------------------------------------------
add_library(alienfx_common STATIC
    CustomMutex.cpp
    ThreadHelper.cpp)
add_library(alienfx::common ALIAS alienfx_common)

target_include_directories(alienfx_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(alienfx_common
    PUBLIC  alienfx::compat
    PRIVATE alienfx::warnings)

if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(alienfx_common PUBLIC Threads::Threads)
endif()
