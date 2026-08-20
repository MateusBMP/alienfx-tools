# Platform Abstraction Layer

Goal: let `AlienFX_SDK.cpp`, `alienfx-controls.h`, and other protocol-logic files stay
almost untouched, by isolating Windows-type and Windows-API usage behind a compat
header rather than rewriting call sites throughout the tree. This is the same technique
`tr1xem/alienfx-linux` uses for HID calls specifically (see
[04](04-alienfx-sdk-hid.md)); this doc generalizes it to the rest of the codebase.

**Implemented as of M1**: `Common/win_compat.h`, plus the `#ifdef _WIN32` seams in
`AlienFX_SDK.h`, `Common/CustomMutex.{h,cpp}`, and `Common/ThreadHelper.{h,cpp}`. Every
code sample and decision below reflects what actually shipped, verified against GCC 16
and Clang 22 under `-std=c++17 -Wall -Wextra -Wpedantic -Werror` (the `gcc`/`clang`
presets), not a plan.

## Why this is possible: Windows types leak into otherwise-portable headers

`<wtypes.h>` (or full `<windows.h>`) is included by files that contain no actual Win32
*calls* — meaning a typedef shim, not a behavioral reimplementation, unblocks them. Of
the eight sites originally surveyed, **only three are in M1's scope**; the milestone doc
originally estimated "a dozen headers" — that was wrong, corrected in
[17-milestones.md](17-milestones.md):

| File | Scope |
|---|---|
| `AlienFX-SDK/AlienFX_SDK/AlienFX_SDK.h:2` | **M1** |
| `Common/CustomMutex.h:2` | **M1** |
| `Common/ThreadHelper.h:2` | **M1** |
| `RegHelperLib/RegHelperLib.h:2` | M4 (registry → config backend) |
| `alienfx-mon/ConfigMon.h:4` | M4 |
| `alienfan-tools/alienfan-SDK/alienfan-SDK.h:5` (both v1 and v2 copies) | Never ported — see [05](05-alienfan-sdk-thermal.md) |
| `alienfx-gui/alienfx-gui.h:3` | M7 (Qt6 GUI) |
| `alienfx-LFX/LFXUtil.cpp:5` | M9 (LightFX) |

`Common/win_compat.h`, Linux-only, included instead of `<windows.h>`/`<wtypes.h>` under
`#ifndef _WIN32` (the actual header carries a fuller rationale comment per symbol; this
is the shape):

```cpp
using BYTE = uint8_t;
using WORD = uint16_t;
using DWORD = uint32_t;
using BOOL = int;
using UCHAR = unsigned char;
using HANDLE = void*;
using LPVOID = void*;
// No HWND: a real GUI dependency, belongs in M7, not compat-shimmed away.
// No HKEY: a registry design decision (M4, doc 06), not a type shim -- see
// "What this doc does not cover" below.
// No `byte`: see the dedicated section below -- shimming it here as a global
// typedef was tried and rejected; the actual fix lives in AlienFX_SDK.h instead.
```

### Decision: where the compat header lives

`Common/win_compat.h` — not a new `compat/` or `platform/` subdirectory, and not a
top-level fork-only directory. This follows the precedent
[02-build-system.md](02-build-system.md) already set for `CMakeLists.txt` placement:
per-directory, inside the existing source tree, upstreamable — the maintainer has
repeatedly asked for the SDKs to stay reusable libraries (see
[01](01-why-no-linux-support.md)), and a fork-only quarantine directory for compat code
fights that goal the same way a fork-only quarantine for CMake files would have.
`Common/` is additionally where two of M1's three consumers already live, and where
doc 02:55 already assigns the eventual `common` CMake target ("platform-abstracted per
03").

This is safe against
[18-windows-verification.md](18-windows-verification.md)'s header-shadowing warning
("M1+ must not casually add e.g. a bare `config.h` at a location that could shadow an
existing Windows header") for a reason specific to this file: it is only ever reached
through the `#else` arm of `#ifdef _WIN32` / `#else` / `#endif`, which MSBuild's
preprocessor never takes. The file is invisible to the Windows build regardless of its
name or location.

Keep this header minimal and additive — its job is to satisfy the type system for
*data-layer* code, not to pretend Win32 APIs exist. If a file needs an actual Win32
*call* (not just a type), that call belongs in a `#ifdef _WIN32` / `#else` split at the
call site, not in this header.

## `byte` — decision reversed from the original plan

The original version of this doc said: don't typedef `byte`, retype
`alienfx-controls.h`'s ~124 data-table declarations to `BYTE`/`uint8_t` instead. That
reasoning about *why* `byte` is dangerous was correct and is preserved below, but the
prescribed fix was more invasive than necessary and has been replaced.

**Why `byte` is dangerous**: the project builds with `using namespace std;` everywhere
(`AlienFX_SDK.h:10` and others), and under C++17 the unqualified name `byte` resolves to
`std::byte`, a scoped enum that rejects the brace-initializer lists
`alienfx-controls.h`'s data tables use (`const byte COMMV1_color[]{ 1, 0x03 };` fails to
compile: `'byte' does not name a type` becomes a `std::byte` init-list error once a
*global* typedef is added).

**What M1 actually does**: `AlienFX_SDK.h` declares `using byte = uint8_t;` **inside**
`namespace AlienFX_SDK { ... }`, immediately after the namespace opens, rather than at
file/global scope:

```cpp
namespace AlienFX_SDK {
#ifndef _WIN32
	using byte = uint8_t;
	// ...
#endif
	// existing header content unchanged
}
```

This works because of ordinary unqualified-name-lookup rules, not because of anything
special about `using`: for code lexically inside `namespace AlienFX_SDK { ... }` — which
is where the entirety of `AlienFX_SDK.cpp`'s body and all of `alienfx-controls.h`'s
tables live — an unqualified reference to `byte` finds the namespace member declared in
its own innermost enclosing scope and stops looking; it never has to consult the
`using namespace std;` injection at all, because that's a *outer*-scope fallback only
reached when the inner scope has no matching declaration. Verified on GCC 16 and Clang
22 under `-std=c++17 -Wall -Wextra -Wpedantic -Werror`.

**Consequence, not a design goal**: this also resolves a pre-existing latent bug.
`AlienFX_SDK.h:208` (pre-M1 numbering) declared `SetMultiColor(vector<byte>*, ...)`
while `AlienFX_SDK.cpp:376` defines it as `SetMultiColor(vector<UCHAR>*, ...)` —
identical on MSVC (`byte` there means `unsigned char`, same as `UCHAR`) but a link error
the instant `byte` means `std::byte` in one TU and not the other. The namespace-scoped
typedef makes both spellings resolve to the same type again, so this never had to be
fixed as its own step.

**Scope boundary this creates, deliberately**: the typedef only helps code lexically
inside `namespace AlienFX_SDK { ... }` (or code that qualifies as `AlienFX_SDK::byte`).
Nothing in this repository currently writes `using namespace AlienFX_SDK;` and then uses
a bare `byte` from outside the namespace — verified by grep — so this isn't a live
footgun today, but it's worth knowing if a future file adopts that pattern: it would see
`byte` as ambiguous between `AlienFX_SDK::byte` and `std::byte` and need to qualify one
of them.

`alienfx-controls.h` itself needed **zero edits** — it has no `#include`s of its own and
depends on being included after `AlienFX_SDK.h` in the same translation unit (exactly
how `AlienFX_SDK.cpp:2-3` already does it), so the namespace-scoped typedef is already
in scope for its ~124 `byte`-typed table declarations by the time they're parsed.

## CRT/safe-function replacements — scope corrected

Counted across the tree (approximate, from source grep): `sscanf_s` (27), `strcpy_s`
(9), `sprintf_s` (5), `memcpy_s` (1, commented out), plus `StringCbCat`/`StringCbPrintf`
(4, `alienfan-low.c` only, never ported).

**M1 does not touch any of these.** Auditing the call sites was originally framed as M1
work; it turned out none of the 44 sites fall inside M1's actual compile scope
(`AlienFX_SDK.h`, `Common/CustomMutex.*`, `Common/ThreadHelper.*`). All 6 `sscanf_s`
calls inside `AlienFX_SDK.cpp` are in `Mappings::LoadMappings` (`:1059-1088`), which is
M4 territory (registry → config backend, see [06](06-configuration-storage.md)); the 2
`strcpy_s` calls are in `Common/Common.cpp`, which M1 explicitly excludes (see "GUI code
excluded from M1", below).

`win_compat.h` ships a `#define sscanf_s sscanf` shim anyway — cheap, and it means M4
doesn't have to touch this file to start using it. It's justified for the SDK's specific
6 call sites: `sscanf_s`/`sscanf` differ only in taking extra `_s`-suffix size arguments
for `%s`/`%c`/`[]` conversions, and all 6 SDK sites use integer-only conversions
(`%hd`/`%d`/`%hhd`), so a blanket rename is safe *there*. **Audit the remaining sites
before relying on the same shim for them** — `ConfigHandler.cpp`, `ConfigFan.cpp`, and
`ConfigMon.cpp`'s calls decode packed registry data and haven't been individually
checked; this is M4's job, not M1's.

## Threading and synchronization

### `CustomMutex`

`Common/CustomMutex.h`/`.cpp` (33 lines) — a thin `SRWLOCK` wrapper
(`InitializeSRWLock`/`AcquireSRWLockShared`/`AcquireSRWLockExclusive`/`Release*`, with a
commented-out dead `CRITICAL_SECTION` path alongside). Maps to `std::shared_mutex`.

**Decision: the public API is unchanged**, not redesigned. An earlier draft of this doc
proposed renaming the four methods (`lockRead`/`lockWrite`/`unlockRead`/`unlockWrite` →
`LockShared`/`UnlockShared`/`LockExclusive`/`UnlockExclusive`). M1 keeps the original
names — only the private `mHandle` member and the five method bodies swap behind
`#ifdef _WIN32`:

```cpp
class CustomMutex {
#ifdef _WIN32
    SRWLOCK mHandle;
#else
    std::shared_mutex mHandle;
#endif
public:
    CustomMutex();
    void lockRead();    // -> mHandle.lock_shared()
    void lockWrite();   // -> mHandle.lock()
    void unlockRead();  // -> mHandle.unlock_shared()
    void unlockWrite(); // -> mHandle.unlock()
};
```

Rationale: the four call sites (`alienfx-gui/FXHelper.h:47`,
`alienfx-gui/ConfigHandler.h:216-217`, `LightFX/LightFX.cpp:28`) all live in files not
built until M7/M9. Renaming now buys nothing and forces those future ports to touch a
line they otherwise wouldn't need to.

Verified with concurrency tests (N readers overlapping, a writer excluding both readers
and other writers), re-run via `ctest --repeat until-fail:50`, and clean under
ThreadSanitizer.

### `ThreadHelper`

`Common/ThreadHelper.h`/`.cpp` (47 lines) — `CreateThread` + a manual-reset
`CreateEvent`, with the periodic-tick pattern `WaitForSingleObject(evt, delay) ==
WAIT_TIMEOUT` (`ThreadHelper.cpp:33-46`, pre-M1 numbering) driving a callback on an
interval; also sets `SetThreadPriority(THREAD_PRIORITY_LOWEST)`.

**Decision: the public API is unchanged here too.** An earlier draft proposed a
redesigned `Start(interval, tick)` / `std::function`-based API. M1 keeps the original
constructor signature, public members (`func`, `tEvent`, `tHandle`, `delay`, `priority`,
`param`), and `Stop()`/`Start()` shape — only the implementation behind them changes.
Rationale: all 8 call sites (`alienfx-gui/CaptureHelper.cpp:47,136`,
`GridHelper.cpp:181-182`, `SysMonHelper.cpp:82`, `WSAudioIn.h:50`,
`alienfan-tools/alienfan-mon/MonHelper.cpp:53`) are in files not built until M7/M8; a
redesigned API would fork the class shape between platforms and force *every one* of
those call sites to carry a second `#ifdef` arm when they're eventually ported — this
defers the churn rather than removing it, for no present benefit.

Maps to `std::thread` + `std::condition_variable::wait_for`, with `tEvent`/`tHandle`
(both `HANDLE` = `void*`) holding pointers to small Linux-only state structs
(`LinuxEvent`, `LinuxThreadState`) rather than real Win32 handles:

```cpp
struct LinuxEvent {  // manual-reset event, matches CreateEvent(NULL, true, false, NULL)
    std::mutex m;
    std::condition_variable cv;
    bool signaled = false;
    bool WaitTimedOut(int ms);  // true if still not signaled when the wait ended
    void Set();
};
struct LinuxThreadState { std::thread thread; };  // one per Start()/Stop() cycle
```

**Two behavioral quirks are deliberately preserved, not "fixed"** — both are load-bearing
today and would change observable behavior for existing (Windows) consumers if altered:

1. **do/while, not while/do.** `ThreadFunc` calls the callback once *before* its first
   wait, not after — some consumers (e.g. `CaptureHelper`'s first-frame paint) rely on
   immediate first-tick behavior. The Linux worker lambda reproduces this exactly:
   `do { func(param); } while (ev->WaitTimedOut(delay));`.
2. **Manual-reset, never reset.** `CreateEvent(NULL, true, false, NULL)` creates a
   manual-reset event, and nothing in this class ever calls `ResetEvent`. So once
   `Stop()` signals the event, a later `Start()` spins up a new worker whose very first
   wait sees an already-signaled event and returns immediately — exactly one tick fires
   (the do/while's unconditional first call), then the worker exits. This is asserted as
   intentional by `tests/common/thread_helper_test.cpp`'s
   `RestartAfterStopReproducesManualResetEventQuirk` test, not left to accident.

**One deliberate divergence from Windows**, forced by `std::thread`'s stricter contract:
Windows' `Stop()` waits `delay << 2` ms for the thread to exit, then calls `CloseHandle`
*regardless of whether it actually did* — which, if the thread is still running, leaks it
(closing a handle doesn't kill the thread; nothing then has a handle to wait on it ever
finishing). `std::thread` offers no equivalent middle ground: the destructor of a
joinable `std::thread` that is never joined calls `std::terminate`. Linux's `Stop()`
therefore **joins unconditionally**, dropping the `delay << 2` timeout hint entirely. A
`detach()` was considered and rejected — it would leave the callback potentially running
against a `ThreadHelper` that has already been destroyed, which is worse than a
slower-but-bounded shutdown (the callback itself is expected to return promptly; nothing
in the tree runs an unbounded operation inside one).

Thread priority (`THREAD_PRIORITY_LOWEST`/`_BELOW_NORMAL`/`_NORMAL`, also used directly
in `LightFX/LightFX.cpp:42`) has no portable C++ equivalent. Linux uses
`setpriority(PRIO_PROCESS, gettid(), niceVal)`, called from *inside* the new thread
itself (so it can target its own kernel TID directly — `std::thread::native_handle()` is
a `pthread_t`, not the same value `setpriority(2)` needs, and there's no portable way to
convert one to the other from outside the thread), gated behind `#ifdef __linux__`,
failure silently ignored. This remains a "nice to have" per the original framing — it
affects only how nicely a thread shares the CPU (e.g. keeping the light-update thread
from starving the UI), never correctness, and nothing in the test suite asserts on the
resulting OS-level niceness.

Verified with tests covering: immediate first-tick, tick count roughly matching the
configured interval, `Stop()` actually joining (no ticks after it returns), `Stop()`
idempotency, destructor-stops-a-running-helper, and the restart quirk above — re-run via
`ctest --repeat until-fail:50` and clean under both AddressSanitizer and
ThreadSanitizer.

## Calling conventions and export macros

- `STDCALL` (used on all 22 `LightFX`/`LightFX/LFX2.h` exports) is a 32-bit-x86-only concept on
  Linux; on x86-64 System V it's a no-op, but the macro itself must still be redefined
  (it currently expands to `__stdcall`, which GCC/Clang don't recognize on x86-64
  without `-fms-extensions`). See [14](14-lightfx-library.md) for the full export-macro
  treatment.
- `__declspec(dllexport)`/`dllimport` (`LightFX/LFX2.h:32-38`, `alienfx-LFX/LFXConfigurator.h:31,34`) →
  `__attribute__((visibility("default")))` behind `#ifdef _WIN32` / `#else`.
- `WINAPI`/`APIENTRY`/`CALLBACK` appear ~109 times across the repo, almost entirely on
  thread procs, dialog procs, and hook procs that belong to Windows-only files (GUI,
  `alienfan-low`) rather than the portable SDK core. **Exception, shipped in M1**:
  `win_compat.h` defines `WINAPI` as empty, because `Common/ThreadHelper.cpp`'s Windows
  arm declares `DWORD WINAPI ThreadFunc(LPVOID)` and the header needs to parse on both
  platforms even though the Linux arm never calls that function. Everywhere else,
  `WINAPI`/`APIENTRY`/`CALLBACK` still don't need a compat shim — they need the
  surrounding function to move behind `#ifdef _WIN32` in
  [10](10-gui-qt6.md)/[13](13-input-and-hotkeys.md), since there's no cross-platform
  concept of a "dialog proc" to abstract.

## Anonymous `struct`-inside-`union` — silencing mechanism corrected

19+ sites concentrated in `AlienFX_SDK.h` (the color/light-mask union types:
`Afx_colorcode`, `Afx_light`, `Afx_groupLight`, and the PID/VID unions on `Functions` and
`Afx_device`). GCC and Clang both accept this MS/C11 extension in C++, but warn.

**The originally-proposed silencing flag was wrong.** The plan was
`-Wno-microsoft-anon-tag` on Clang, "no direct GCC equivalent." Verified against GCC 16
and Clang 22 under `-std=c++17` (this project's actual standard —
`CMAKE_CXX_EXTENSIONS OFF` means `-std=c++17`, not `-std=gnu++17`): Clang instead emits
`-Wgnu-anonymous-struct`, and GCC emits it as plain `-Wpedantic` — which `alienfx::warnings`
(`cmake/AlienfxWarnings.cmake`) already enables project-wide, with `-Werror` on the
`gcc`/`clang` presets. The originally-proposed flag would not have suppressed either
compiler's actual warning; the build would have failed the moment this header was first
compiled.

**What M1 actually ships**: a scoped `#pragma GCC diagnostic push` /
`ignored "-Wpedantic"` (both compilers recognize `-Wpedantic` and understand
`#pragma GCC diagnostic`, including Clang) plus, additionally on Clang only,
`ignored "-Wgnu-anonymous-struct"` — GCC does not recognize that flag name and hard-errors
under `-Werror=unknown-pragmas`/`-Werror=pragmas` if it's not conditionally compiled
out:

```cpp
#ifndef _WIN32
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wpedantic"
	#ifdef __clang__
	#pragma GCC diagnostic ignored "-Wgnu-anonymous-struct"
	#endif
#endif
	// ... union types ...
#ifndef _WIN32
	#pragma GCC diagnostic pop
#endif
```

Scoped to the whole `namespace AlienFX_SDK { ... }` body (matched push/pop at the
namespace's open and close) rather than per-union, since the anon-struct usage is
pervasive throughout the header and per-union scoping would add noise without changing
what's silenced. Prefer silencing over rewriting — these are the on-wire/on-disk data
model, and reworking them risks subtly changing packing/alignment. Layout is instead
pinned down positively, at the individual field level, by
`tests/alienfx_sdk/sdk_headers_test.cpp`'s `offsetof`/`sizeof` `static_assert`s (see
[17-milestones.md](17-milestones.md) — this is the check that makes M1's flagged
review-risk an actually-verified property rather than an assertion in a doc).

## A new finding from M1: aggregate-init warnings in `AlienFX_SDK.cpp` (M2's problem, recorded here)

`AlienFX_SDK.cpp:918,999,1077` (pre-M1 line numbers, `AlienFX_SDK.cpp` is otherwise
unmodified by M1) all brace-elide into `Afx_device`'s or `Afx_light`'s anonymous union
while leaving trailing members — which have default member initializers, e.g.
`Afx_device::lights` — unlisted. This is valid C++17 aggregate initialization, but both
compilers warn regardless: `-Wmissing-field-initializers` (GCC and Clang) and
`-Wmissing-braces` (Clang only). Verified by reproducing these three call sites verbatim,
lexically inside `namespace AlienFX_SDK { ... }` exactly as the real file has them: this
fails to compile under `-Werror` on both compilers today.

This is **not** an M1 action item — `AlienFX_SDK.cpp` isn't ported until M2 — but it's
recorded here so M2 doesn't have to rediscover it mid-port. Two options when M2 gets
there: fully brace-initialize every member at those three call sites, or scope
`#pragma GCC diagnostic ignored "-Wmissing-field-initializers"` (+ `"-Wmissing-braces"`
on Clang) around them, the same pattern used above for the anon-struct warnings.
`tests/alienfx_sdk/sdk_headers_test.cpp`'s `BraceElisionIntoAnonymousUnionCompiles` test
reproduces the pattern (with the pragma) so the underlying union brace-elision mechanism
stays exit-criteria-checked without asserting that the real file's exact call sites are
warning-clean today, which they are not.

## GUI code excluded from M1, with a landmine flagged for M7

`Common/common.h` (17 declarations) + `Common/Common.cpp` are **not** part of M1's
`common` CMake target, despite living in the same directory as `CustomMutex`/
`ThreadHelper`. 13 of `common.h`'s 17 declarations take `HWND` or `NOTIFYICONDATA*`
directly — this is GUI surface, not shared infrastructure, and the "HWND and friends
should NOT be shimmed here" rule above already rules it out of `win_compat.h`. It
belongs to [10-gui-qt6.md](10-gui-qt6.md) (M7).

**Landmine for whoever picks that up**: the file is `common.h` on disk (lowercase), but
15 of its 19 include sites across the tree spell it `"Common.h"` — including
`Common/Common.cpp:1` itself. This works today only because MSVC's filesystem is
case-insensitive; it will fail to resolve on any case-sensitive Linux filesystem the
moment M7 tries to compile a file that includes it under the wrong-case spelling.
Fix it then (either rename the file, which is a Windows-build-visible change and needs
sign-off per this repo's working agreements, or normalize every include site to the
correct case) — not now, since nothing in M1's scope includes this file at all.

## What this doc does not cover

Registry APIs (`RegCreateKeyEx`, `RegGetValue`, etc.) are not a "compat shim" problem —
their Linux replacement is a real design decision about config format and location,
covered in [06](06-configuration-storage.md). Don't try to fake a `HKEY`-shaped API
over files; replace the config classes' internals instead. Concretely for M1: `HKEY`
appears in `AlienFX_SDK.cpp` only inside `Mappings::LoadMappings`/`SaveMappings`
(`:1045-1156`, pre-M1 numbering) — M4 territory — so `win_compat.h` does not define it,
matching the treatment `HWND` already got in an earlier draft of this doc.
