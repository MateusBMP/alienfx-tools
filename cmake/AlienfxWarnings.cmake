# Warning policy for the Linux port.
#
# This is an INTERFACE target, not add_compile_options(), so vendored code (kiss_fft)
# and anything pulled in later via alienfx_require_package()/FetchContent can opt out
# simply by not linking alienfx::warnings. Only first-party targets should link it.
#
# -Wconversion and -Wshadow are deliberately NOT enabled yet: they fire heavily across
# the ported SDK's packed color/mask types and offset arithmetic (M1/M2). Revisit once
# that code exists instead of guessing at exceptions now.

include_guard(GLOBAL)

add_library(alienfx_warnings INTERFACE)
add_library(alienfx::warnings ALIAS alienfx_warnings)

set(_afx_warn -Wall -Wextra -Wpedantic)

target_compile_options(alienfx_warnings INTERFACE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:${_afx_warn}>
    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:${_afx_warn}>)

if(ALIENFX_WERROR)
    target_compile_options(alienfx_warnings INTERFACE
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Werror>
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Werror>)
endif()
