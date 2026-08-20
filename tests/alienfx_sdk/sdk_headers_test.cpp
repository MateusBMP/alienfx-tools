// M1 exit-criteria tests for AlienFX-SDK/AlienFX_SDK/AlienFX_SDK.h +
// alienfx-controls.h: they must compile on Linux without pulling in real Windows
// headers, AND (the criterion M1 adds on top of the milestone doc's bare
// "compiles") the layout of the on-wire union types the HID protocol is built on
// must be pinned down, since those are exactly what M2 depends on.
//
// No hardware, no golden files -- this is a headers-only milestone. M2 replaces
// alienfx_sdk_headers with a real STATIC library and adds packet-builder tests
// (golden byte vectors, see tests/README.md) to this same test binary.

#include "AlienFX_SDK.h"
#include "alienfx-controls.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <new>

using namespace AlienFX_SDK;

namespace {

// --- `byte` resolves to unsigned char, not std::byte ------------------------
//
// AlienFX_SDK.h declares a namespace-scoped `using byte = uint8_t;` specifically
// so this file's `using namespace std;` (AlienFX_SDK.h:10) doesn't make the
// unqualified name `byte` resolve to std::byte, which would reject every
// brace-initializer-list table in alienfx-controls.h.
static_assert(std::is_same_v<AlienFX_SDK::byte, uint8_t>,
              "AlienFX_SDK::byte must be uint8_t, not std::byte");

TEST(SdkHeaders, ControlsTablesFirstBytesMatchTheProtocol) {
    // alienfx-controls.h has no #include of its own; it depends on being
    // included after AlienFX_SDK.h in the same translation unit (as
    // AlienFX_SDK.cpp:2-3 does) for `byte` to already mean uint8_t. This test IS
    // that translation unit. If `byte` had resolved to std::byte instead, none
    // of these tables would have compiled at all.
    EXPECT_EQ(reportIDList[4], 0);
    EXPECT_EQ(reportIDList[5], 0xcc);
    EXPECT_EQ(brightnessScale[5], 0xff);
    // v4OpCodes and v7OpCodes are declared `static` (not `const`) at namespace
    // scope, unlike the rest of this header's tables -- GCC's -Wunused-variable
    // fires on them specifically if a TU doesn't reference them, so both are
    // touched here alongside the const ones.
    EXPECT_EQ(v4OpCodes[0], 0xd0);
    EXPECT_EQ(v6OpCodes[0], 0x87);
    EXPECT_EQ(v7OpCodes[0], 1);
    EXPECT_EQ(v8OpCodes[0], 0x81);
}

// --- Union layout: the on-wire/on-disk data model ---------------------------
//
// AlienFX_SDK.h:36-46 (Afx_colorcode), :55-61 (Afx_light), :65-70
// (Afx_groupLight), :161-166 (Functions' pid/vid/devID), :252-257 (Afx_device's
// pid/vid/devID). These asserts are what makes the milestone's flagged risk
// ("especially the anonymous struct-in-union handling ... worth extra review
// time here", Doc/linux_roadmap/17-milestones.md) an actually-checked property
// instead of an assertion in a doc.

static_assert(sizeof(Afx_colorcode) == sizeof(DWORD));
static_assert(offsetof(Afx_colorcode, b) == 0);
static_assert(offsetof(Afx_colorcode, g) == 1);
static_assert(offsetof(Afx_colorcode, r) == 2);
static_assert(offsetof(Afx_colorcode, br) == 3);

static_assert(sizeof(Afx_groupLight) == sizeof(DWORD));
static_assert(offsetof(Afx_groupLight, did) == 0);
static_assert(offsetof(Afx_groupLight, lid) == 2);

static_assert(offsetof(Afx_light, flags) == offsetof(Afx_light, data) + 0);
static_assert(offsetof(Afx_light, scancode) == offsetof(Afx_light, data) + 2);

static_assert(offsetof(Afx_device, pid) == offsetof(Afx_device, devID) + 0);
static_assert(offsetof(Afx_device, vid) == offsetof(Afx_device, devID) + 2);

// Functions' pid/vid/devID union (AlienFX_SDK.h:161-166) can't use offsetof:
// Functions has private members ahead of that public union, which makes it
// non-standard-layout, and both GCC and Clang hard-error on offsetof for such a
// type under -Werror=invalid-offsetof (verified). See
// FunctionsDevIdMatchesMakelparamOfPidVid below for how it's covered instead.

TEST(SdkHeaders, ColorcodeUnionReadsBackInDocumentedOrder) {
    // Writing cf and reading b/g/r/br back (or vice versa) must round-trip in
    // the order the struct declares them -- this pins little-endian, which every
    // build target here is, but makes the assumption explicit rather than
    // silently relied upon.
    Afx_colorcode c(0x11223344u);
    EXPECT_EQ(c.b, 0x44);
    EXPECT_EQ(c.g, 0x33);
    EXPECT_EQ(c.r, 0x22);
    EXPECT_EQ(c.br, 0x11);

    Afx_colorcode c2(0xaa, 0xbb, 0xcc, 0xdd);
    EXPECT_EQ(static_cast<DWORD>(c2.cf), 0xddccbbaau);
}

TEST(SdkHeaders, GroupLightUnionRoundTrips) {
    Afx_groupLight g{};
    g.lgh = 0x00050003u;
    EXPECT_EQ(g.did, 3);
    EXPECT_EQ(g.lid, 5);
}

TEST(SdkHeaders, FunctionsDevIdMatchesMakelparamOfPidVid) {
    // Functions declares ~Functions() but doesn't define it (the body lives in
    // AlienFX_SDK.cpp, which M2 -- not M1 -- ports), so a normal stack instance
    // would fail to link when it goes out of scope. Placement-new into raw
    // storage sidesteps that: the implicit default constructor is fine (it's
    // trivial/inline, no out-of-line definition needed), and this object's
    // destructor is deliberately never invoked -- a small, intentional,
    // single-object leak for the duration of one test, not a pattern to reuse
    // outside this narrow M1 constraint.
    alignas(Functions) unsigned char storage[sizeof(Functions)];
    Functions* f = new (storage) Functions();
    f->pid = 0x0550;
    f->vid = 0x187c;
    EXPECT_EQ(static_cast<uint32_t>(f->devID),
              static_cast<uint32_t>(MAKELPARAM(f->pid, f->vid)));
}

TEST(SdkHeaders, DeviceDevIdMatchesMakelparamOfPidVid) {
    // The SDK builds a combined device ID exactly this way (AlienFX_SDK.cpp:
    // 1061,1065,1069): MAKELPARAM(pid, vid). Confirms the compat macro and the
    // union layout agree with each other, not just individually with Windows.
    Afx_device d;
    d.pid = 0x0550;
    d.vid = 0x187c;
    EXPECT_EQ(static_cast<uint32_t>(d.devID),
              static_cast<uint32_t>(MAKELPARAM(d.pid, d.vid)));
}

// --- Aggregate init into the anonymous unions -------------------------------
//
// AlienFX_SDK.cpp:918,999,1077 all brace-elide into Afx_device/Afx_light's
// anonymous union while leaving trailing members (which have default member
// initializers, e.g. Afx_device::lights) unlisted -- valid C++17 aggregate
// init, but GCC/Clang warn regardless: -Wmissing-field-initializers (both) and
// -Wmissing-braces (Clang only, plus its own -Wmissing-field-initializers).
// Verified: reproducing these three call sites verbatim, in a namespace-nested
// TU exactly like AlienFX_SDK.cpp itself, fails under -Werror on both compilers.
//
// This is a genuine M2 finding, not an M1 one -- AlienFX_SDK.cpp isn't ported
// yet, so nothing here needs a code change today. It's recorded so M2 doesn't
// have to rediscover it mid-port: either fully brace-initialize every member at
// those three call sites, or scope
// #pragma GCC diagnostic ignored "-Wmissing-field-initializers" (+
// "-Wmissing-braces" on Clang) around them when AlienFX_SDK.cpp is ported. This
// test reproduces the pattern with the local pragma so the *union brace-elision
// itself* stays exit-criteria-checked without also asserting a false claim that
// the real file's exact call sites are warning-clean today.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wmissing-braces"
#endif
#endif

TEST(SdkHeaders, BraceElisionIntoAnonymousUnionCompiles) {
    Afx_device d{0x0550, 0x187c, nullptr, "test"};
    EXPECT_EQ(d.pid, 0x0550);
    EXPECT_EQ(d.vid, 0x187c);

    Afx_light l{1, {LOWORD(0x00010002u), HIWORD(0x00010002u)}, "name"};
    EXPECT_EQ(l.flags, 0x0002);
    EXPECT_EQ(l.scancode, 0x0001);
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

}  // namespace
