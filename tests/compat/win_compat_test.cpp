// Tests for Common/win_compat.h -- the Linux stand-in for <wtypes.h>/<windows.h>.
// No hardware, no golden files. Every packing-macro assertion is checked against
// values hand-derived from the Windows winnt.h/minwindef.h definitions
// (see win_compat.h's own header comment for the derivation), not re-derived here
// -- getting these wrong silently corrupts light/grid geometry
// (AlienFX_SDK.cpp's device-ID and grid-position packing).

#include "win_compat.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

namespace {

// --- Type shims ------------------------------------------------------------

TEST(WinCompatTypes, SizesAndSignednessMatchWindows) {
    static_assert(sizeof(BYTE) == 1 && std::is_unsigned_v<BYTE>);
    static_assert(sizeof(WORD) == 2 && std::is_unsigned_v<WORD>);
    static_assert(sizeof(DWORD) == 4 && std::is_unsigned_v<DWORD>);
    static_assert(sizeof(UCHAR) == 1 && std::is_unsigned_v<UCHAR>);
    static_assert(sizeof(HANDLE) == sizeof(void*));
    static_assert(sizeof(LPVOID) == sizeof(void*));
    SUCCEED();
}

TEST(WinCompatTypes, BoolConstantsMatchWindows) {
    EXPECT_EQ(TRUE, 1);
    EXPECT_EQ(FALSE, 0);
}

// --- Packing macros --------------------------------------------------------

TEST(WinCompatMacros, LowordHiwordSplitADword) {
    constexpr DWORD v = 0x1234abcdu;
    EXPECT_EQ(LOWORD(v), 0xabcd);
    EXPECT_EQ(HIWORD(v), 0x1234);
}

TEST(WinCompatMacros, LowordHiwordHandleTopBitSet) {
    // Sign-sensitive edge: v has the DWORD's high bit set. LOWORD/HIWORD must not
    // sign-extend through an intermediate signed type on the way to a WORD.
    constexpr DWORD v = 0x8000ffffu;
    EXPECT_EQ(HIWORD(v), 0x8000);
    EXPECT_EQ(LOWORD(v), 0xffff);
}

TEST(WinCompatMacros, LobyteHibyteSplitAWord) {
    constexpr WORD v = 0xbeef;
    EXPECT_EQ(LOBYTE(v), 0xef);
    EXPECT_EQ(HIBYTE(v), 0xbe);
}

TEST(WinCompatMacros, HibyteLobyteRoundTripEveryWordValue) {
    // Exhaustive over all 65536 WORD values: HIBYTE/LOBYTE split, then MAKEWORD
    // reassembly, must recover the original word exactly. Mirrors
    // AlienFX_SDK.cpp's actual use -- a grid X/Y position is packed as
    // ((DWORD)x << 8) | y (:1149) and later unpacked with HIBYTE/LOBYTE (:1082).
    for (uint32_t v = 0; v <= 0xffff; ++v) {
        const WORD w = static_cast<WORD>(v);
        const WORD rebuilt = MAKEWORD(LOBYTE(w), HIBYTE(w));
        ASSERT_EQ(rebuilt, w) << "round trip failed for w=" << w;
    }
}

TEST(WinCompatMacros, MakewordRebuildsAWordFromBytes) {
    EXPECT_EQ(MAKEWORD(0xef, 0xbe), 0xbeef);
    EXPECT_EQ(MAKEWORD(0, 0), 0);
    EXPECT_EQ(MAKEWORD(0xff, 0xff), 0xffff);
}

TEST(WinCompatMacros, MakelparamPacksTwoWordsIntoASignedDword) {
    // AlienFX_SDK.cpp:1061,1065,1069 use this exact call shape to build a
    // combined device ID from (pid, vid).
    EXPECT_EQ(static_cast<uint32_t>(MAKELPARAM(0x0550, 0x187c)), 0x187c0550u);
}

TEST(WinCompatMacros, MakelparamAllOnesStaysMinusOneAsInt32) {
    // Sign-sensitive edge noted in the M1 plan: MAKELPARAM(0xffff, 0xffff) must
    // land on int32_t(-1), not some UB/implementation-defined bit pattern from an
    // intermediate signed overflow.
    EXPECT_EQ(MAKELPARAM(0xffff, 0xffff), static_cast<int32_t>(-1));
}

TEST(WinCompatMacros, MakelparamThenLowordHiwordRoundTrips) {
    constexpr WORD lo = 0x0550, hi = 0x187c;
    const auto packed = static_cast<uint32_t>(MAKELPARAM(lo, hi));
    EXPECT_EQ(LOWORD(packed), lo);
    EXPECT_EQ(HIWORD(packed), hi);
}

// --- Thread priority constants ----------------------------------------------

TEST(WinCompatConstants, ThreadPriorityValuesMatchWindows) {
    EXPECT_EQ(THREAD_PRIORITY_LOWEST, -2);
    EXPECT_EQ(THREAD_PRIORITY_BELOW_NORMAL, -1);
    EXPECT_EQ(THREAD_PRIORITY_NORMAL, 0);
}

TEST(WinCompatConstants, WaitTimeoutMatchesWindows) {
    EXPECT_EQ(WAIT_TIMEOUT, 258);
}

// --- CRT shim ------------------------------------------------------------

TEST(WinCompatCrt, SscanfSShimParsesIntegerConversions) {
    // Every M1-in-scope caller (there are none yet -- see win_compat.h's own
    // comment) uses integer conversions only (%hd/%d/%hhd); this pins the shim
    // for when M4 starts calling it.
    short vid = 0, pid = 0;
    EXPECT_EQ(sscanf_s("Dev#4348_1360", "Dev#%hd_%hd", &vid, &pid), 2);
    EXPECT_EQ(vid, 4348);
    EXPECT_EQ(pid, 1360);
}

}  // namespace
