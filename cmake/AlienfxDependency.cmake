# Dependency-acquisition policy for the Linux port.
#
# Doc/linux_roadmap/02-build-system.md: system package FIRST, source fallback SECOND.
# Distro packaging (AUR/deb/rpm) expects find_package()-able system libraries, and
# packaged builds are frequently run with network access sandboxed away entirely.
#
# Implemented on top of FetchContent's FIND_PACKAGE_ARGS (CMake >= 3.24), which
# performs the find_package() attempt itself -- so the policy is expressed with stock
# CMake rather than a bespoke if/else, and a packager gets stock knobs for free:
#   -DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER   force building every dep from source
#   -DFETCHCONTENT_FULLY_DISCONNECTED=ON         forbid downloads; a missing system
#                                                 dep becomes a loud configure failure
#   -DCMAKE_REQUIRE_FIND_PACKAGE_<name>=ON       make one specific dep system-only
#
# RULE: every third-party dependency in this project goes through this function.
# A bare FetchContent_Declare() without FIND_PACKAGE_ARGS defaults to OPT_IN mode --
# i.e. silently network-only -- which breaks distro packaging with no error at all.
#
# Exercised in both acquisition modes by the CMakePresets.json `gcc-fetched` (forces
# the FetchContent branch) and `system-only` (forces the find_package-or-fail branch)
# presets -- run locally; there is no CI to do this automatically.

include_guard(GLOBAL)
include(FetchContent)

# alienfx_require_package(<name>
#     FIND_ARGS <args appended after find_package(<name> ...)>
#     [GIT_REPOSITORY <url>] [GIT_TAG <tag>]
#     [OPTIONS <VAR=VALUE>...])
function(alienfx_require_package name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG
        "" "GIT_REPOSITORY;GIT_TAG" "FIND_ARGS;OPTIONS")

    foreach(_kv IN LISTS ARG_OPTIONS)
        string(REGEX MATCH "^([^=]+)=(.*)$" _m "${_kv}")
        set(${CMAKE_MATCH_1} "${CMAKE_MATCH_2}" CACHE INTERNAL
            "set by alienfx_require_package(${name})")
    endforeach()

    set(_extra "")
    # SYSTEM: CMake >= 3.25. EXCLUDE_FROM_ALL: CMake >= 3.28. Our floor is 3.24, so
    # both are opportunistic -- guarded here rather than raising the project floor.
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
        list(APPEND _extra SYSTEM)
    endif()
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
        list(APPEND _extra EXCLUDE_FROM_ALL)
    endif()

    # Gotcha for future dependencies (M2: hidapi/libusb): CMake 4.x hard-errors on a
    # dependency whose own CMakeLists.txt declares cmake_minimum_required(VERSION
    # <3.5). Prefer pinning a modern tag; if genuinely unavoidable, scope
    # CMAKE_POLICY_VERSION_MINIMUM (CMake >= 4.0) around that one call only -- never
    # set it project-wide.

    FetchContent_Declare(${name}
        GIT_REPOSITORY    ${ARG_GIT_REPOSITORY}
        GIT_TAG           ${ARG_GIT_TAG}
        GIT_SHALLOW       TRUE
        ${_extra}
        FIND_PACKAGE_ARGS ${ARG_FIND_ARGS})

    FetchContent_MakeAvailable(${name})

    if(${name}_SOURCE_DIR)
        message(STATUS "alienfx: dependency '${name}' -> built from source (FetchContent)")
    else()
        message(STATUS "alienfx: dependency '${name}' -> system package (find_package)")
    endif()
endfunction()
