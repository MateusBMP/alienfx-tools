# Vendored KISS FFT (BSD-3-Clause; see the notice atop _kiss_fft_guts.h).
#
# This source list intentionally diverges from kiss_fft.vcxproj:
#
#  * tools/kiss_fftr.c and tools/kiss_fftr.cpp are BYTE-IDENTICAL (verified with
#    `diff`). The .vcxproj compiles only the .cpp. Any file(GLOB) over tools/ would
#    compile both and produce duplicate symbols -- this project never globs sources,
#    for exactly this reason.
#  * tools/kiss_fastfir.c and tools/kiss_fftnd.c are unused: the only consumer in the
#    tree, alienfx-gui/WSAudioIn.cpp:9, needs kiss_fftr only. kiss_fastfir.c is
#    additionally not warning-clean (2 warnings on GCC, 3 on Clang under
#    -Wall -Wextra) and pulls in <sys/mman.h> under FAST_FILT_UTIL.
#  * tools/kfc.c is not in kiss_fft.vcxproj at all. Both kfc.c and kiss_fastfir.c
#    define main(), guarded by KFC_TEST / FAST_FILT_UTIL respectively -- never define
#    those macros on this target.
#
# Do not pass -DFIXED_POINT or -Dkiss_fft_scalar=...: kiss_fft.h defaults the scalar
# type to float via #ifndef, which is what WSAudioIn.cpp assumes.
#
# Not linked against alienfx::warnings -- this is vendored third-party code, not
# first-party code the warning policy is meant to police.
add_library(kiss_fft STATIC
    kiss_fft.cpp
    tools/kiss_fftr.cpp)
add_library(alienfx::kiss_fft ALIAS kiss_fft)

# Consumers write #include <tools/kiss_fftr.h> (see WSAudioIn.cpp:9), and
# tools/kiss_fftr.h in turn does #include "kiss_fft.h" -- so the PUBLIC include root
# is this directory, not tools/.
target_include_directories(kiss_fft PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
