#pragma once
// Linux stand-in for <wtypes.h>/<windows.h>, included instead of them under
// #ifndef _WIN32. See Doc/linux_roadmap/03-platform-abstraction.md for the full
// rationale and the M1 scope decision that produced this exact list.
//
// Rules for anyone extending this file:
//  - Typedefs and trivial bit-twiddling macros only. No Win32 *behavior* -- a file
//    that needs a real Win32 call gets a #ifdef _WIN32 / #else split at the call
//    site, not a fake implementation here (see 03's "What this doc does not cover").
//  - No HWND, no HKEY, no `byte`: HWND is a real GUI dependency (belongs in M7's
//    GUI port, not here); HKEY is a registry design decision (M4's config backend,
//    doc 06), not a type shim; `byte` collides with std::byte once `using namespace
//    std;` is in scope (AlienFX_SDK.h:10) -- the fix lives in AlienFX_SDK.h as a
//    namespace-scoped `using byte = uint8_t;`, not here.
//  - Every macro below must match the Windows definition bit-for-bit. These are
//    on-wire/on-disk packing macros (AlienFX_SDK.cpp's device-ID and grid-position
//    packing); a "simplified" reimplementation silently corrupts data.

#ifdef _WIN32
#error "win_compat.h is Linux-only; include <wtypes.h> directly on Windows"
#endif

#include <cstdint>
#include <cstdio>

// --- Basic types -------------------------------------------------------------
using BYTE = uint8_t;
using WORD = uint16_t;
using DWORD = uint32_t;
using BOOL = int;
using UCHAR = unsigned char;
using HANDLE = void*;
using LPVOID = void*;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// --- Calling convention --------------------------------------------------------
// __stdcall is a 32-bit-x86-only concept; on x86-64 System V (and on any other
// Linux target) it's simply not applicable, so this expands to nothing rather
// than to __attribute__((stdcall)). See Doc/linux_roadmap/03, "Calling
// conventions and export macros".
#define WINAPI

// --- Packing macros --------------------------------------------------------
// Bit-for-bit identical to the winnt.h/minwindef.h definitions. Verified against
// AlienFX_SDK.cpp's actual uses: MAKELPARAM(pid, vid) builds a combined device ID
// (:1061,1065,1069) later unpacked with LOWORD/HIWORD (:999,1077), and
// HIBYTE/LOBYTE (:1082) extract a WORD written back as ((DWORD)x << 8) | y
// (:1149) -- a grid X/Y position. Get these wrong and light/grid geometry breaks
// silently.
#define LOWORD(l) ((WORD)((DWORD)(l) & 0xffff))
#define HIWORD(l) ((WORD)(((DWORD)(l) >> 16) & 0xffff))
#define LOBYTE(w) ((BYTE)((WORD)(w) & 0xff))
#define HIBYTE(w) ((BYTE)(((WORD)(w) >> 8) & 0xff))
#define MAKEWORD(lo, hi) ((WORD)(((BYTE)(lo)) | (((WORD)((BYTE)(hi))) << 8)))
#define MAKELPARAM(lo, hi) \
    ((int32_t)(((WORD)(lo)) | (((DWORD)((WORD)(hi))) << 16)))

// --- Thread priority constants --------------------------------------------------
// All three, even though only THREAD_PRIORITY_LOWEST is used inside M1's compile
// scope (ThreadHelper.h's constructor default argument, which must resolve at
// header-parse time) -- CaptureHelper.cpp and MonHelper.cpp (M7/M8 consumers of
// ThreadHelper) pass the other two explicitly, so all three need to exist once
// this header is the only thing standing in for <wtypes.h> on those files' path.
#define THREAD_PRIORITY_LOWEST       (-2)
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_NORMAL       0

// --- Wait/event result codes ------------------------------------------------
#define WAIT_TIMEOUT 258

// --- CRT "safe function" shims ------------------------------------------------
// sscanf_s differs from sscanf only in taking extra _s-suffix size arguments for
// %s/%c/[] conversions. Every M1-in-scope call site (none yet -- see doc 03, the
// audit is scoped to M4's registry-loading code) uses integer conversions only
// (%hd/%d/%hhd), so a bare rename is safe there; this shim exists so headers that
// declare such calls compile, not because M1 itself calls it.
#define sscanf_s sscanf
