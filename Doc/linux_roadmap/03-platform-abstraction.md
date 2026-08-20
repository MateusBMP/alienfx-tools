# Platform Abstraction Layer

Goal: let `AlienFX_SDK.cpp`, `alienfx-controls.h`, and other protocol-logic files stay
almost untouched, by isolating Windows-type and Windows-API usage behind a compat
header rather than rewriting call sites throughout the tree. This is the same technique
`tr1xem/alienfx-linux` uses for HID calls specifically (see
[04](04-alienfx-sdk-hid.md)); this doc generalizes it to the rest of the codebase.

## Why this is possible: Windows types leak into otherwise-portable headers

`<wtypes.h>` (or full `<windows.h>`) is included by files that contain no actual Win32
*calls* — meaning a typedef shim, not a behavioral reimplementation, unblocks them:

- `AlienFX-SDK/AlienFX_SDK/AlienFX_SDK.h:2`
- `Common/CustomMutex.h:2`
- `Common/ThreadHelper.h:2`
- `RegHelperLib/RegHelperLib.h:2`
- `alienfan-tools/alienfan-SDK/alienfan-SDK.h:5` (both v1 and v2 copies)
- `alienfx-mon/ConfigMon.h:4`
- `alienfx-gui/alienfx-gui.h:3`
- `alienfx-LFX/LFXUtil.cpp:5`

Proposed `win_compat.h` (Linux-only, included instead of `<windows.h>`/`<wtypes.h>`
under `#ifndef _WIN32`):

```cpp
using BYTE = uint8_t;
using WORD = uint16_t;
using DWORD = uint32_t;
using BOOL = int;
using HANDLE = void*;
using HKEY = void*;
using LPVOID = void*;
// HWND and friends should NOT be shimmed here — anything still referencing HWND after
// 03 is a real GUI dependency and belongs in 10, not compat-shimmed away.
```

**Do not add a `byte` typedef to this list — verified during M0.** The project builds
with `using namespace std;` everywhere (`AlienFX_SDK.h:10` and others), and under C++17
(the standard this port targets, see [02](02-build-system.md)) the unqualified name
`byte` resolves to `std::byte`, a scoped enum that rejects the brace-initializer lists
`alienfx-controls.h`'s data tables use (`const byte COMMV1_color[]{ 1, 0x03 };` fails to
compile: `'byte' does not name a type` becomes a `std::byte` init-list error once a
typedef is added). The actual fix when M1 ports `alienfx-controls.h` is to retype its
tables to `BYTE`/`uint8_t`, not to shim `byte` itself.

Keep this header minimal and additive — its job is to satisfy the type system for
*data-layer* code, not to pretend Win32 APIs exist. If a file needs an actual Win32
*call* (not just a type), that call belongs in a `#ifdef _WIN32` / `#else` split at the
call site, not in this header.

## CRT/safe-function replacements

Counted across the tree (approximate, from source grep):

| MSVC function | Count | Replacement |
|---|---|---|
| `sscanf_s` | 27 | `sscanf` (drop the extra buffer-size args) — heaviest in `AlienFX_SDK.cpp:1059-1088` (`Mappings::LoadMappings`) and the `ConfigHandler`/`ConfigFan` registry loaders |
| `strcpy_s` | 9 | `strncpy`/`std::string` assignment |
| `sprintf_s` | 5 | `snprintf` |
| `memcpy_s` | 1 | `memcpy` (with an explicit bounds check at the call site) |
| `StringCbCat` / `StringCbPrintf` (strsafe.h) | 4 | only in `alienfan-low.c` (dead on Linux — see [05](05-alienfan-sdk-thermal.md), this file isn't ported) |

Since `sscanf_s`/`sscanf` differ only in taking extra `_s`-suffix size arguments for
`%s`/`%c`/`[]` conversions, a macro shim is viable if the call sites are regular enough
— but audit each one; don't blanket `#define sscanf_s(...) sscanf(...)` without
checking that no call site actually relies on the extra bounds-checking arguments for
correctness (a few might, since the format strings decode packed registry data).

## Threading and synchronization

`Common/CustomMutex.h`/`.cpp` (33 lines) — a thin `SRWLOCK` wrapper
(`InitializeSRWLock`/`AcquireSRWLockShared`/`AcquireSRWLockExclusive`/
`Release*`, with a commented-out dead `CRITICAL_SECTION` path alongside). Maps directly
to `std::shared_mutex`:

```cpp
class CustomMutex {
    std::shared_mutex m;
public:
    void LockShared()    { m.lock_shared(); }
    void UnlockShared()  { m.unlock_shared(); }
    void LockExclusive()   { m.lock(); }
    void UnlockExclusive() { m.unlock(); }
};
```

`Common/ThreadHelper.h`/`.cpp` (47 lines) — `CreateThread` + a manual-reset
`CreateEvent`, with the periodic-tick pattern `WaitForSingleObject(evt, delay) ==
WAIT_TIMEOUT` (`ThreadHelper.cpp:33-46`) driving a callback on an interval; also sets
`SetThreadPriority(THREAD_PRIORITY_LOWEST)`. Maps to `std::thread` +
`std::condition_variable::wait_for`:

```cpp
class ThreadHelper {
    std::thread worker;
    std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> stop{false};
public:
    void Start(std::chrono::milliseconds interval, std::function<void()> tick) {
        worker = std::thread([=] {
            std::unique_lock lk(m);
            while (!stop) {
                if (cv.wait_for(lk, interval) == std::cv_status::timeout) tick();
            }
        });
    }
    void Stop() { stop = true; cv.notify_all(); worker.join(); }
};
```

`THREAD_PRIORITY_LOWEST`/`THREAD_PRIORITY_NORMAL` (also used directly in
`LightFX/LightFX.cpp:42`) has no portable C++ equivalent; on Linux use `nice()` or
`pthread_setschedparam` on the underlying `std::thread::native_handle()`, gated behind
`#ifdef __linux__` since it's a "nice to have" (avoid the light-update thread starving
the UI), not a correctness requirement.

Consumers of `ThreadHelper`: `alienx-gui`'s `WSAudioIn.cpp`, `CaptureHelper.cpp`
(ambient capture, 100 ms tick — see [11](11-ambient-capture.md)),
`alienfan-tools/alienfan-mon/MonHelper.cpp` (sensor polling — see
[09](09-daemon-and-monitor.md)).

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
  `alienfan-low`) rather than the portable SDK core — these don't need a compat shim,
  they need the surrounding function to move behind `#ifdef _WIN32` in
  [10](10-gui-qt6.md)/[13](13-input-and-hotkeys.md), since there's no cross-platform
  concept of a "dialog proc" to abstract.

## Other portability issues to shim or fix directly

- **`TCHAR`/`_T()` duality** across 15 files (`Common/Common.cpp`, `common.h`, both
  `alienfan-SDK` variants, `alienfan-low.c/.h`, `alienfan-cli.cpp`, `alienfan-gui.cpp`,
  `alienfx-LFX/LFXUtil.cpp`, `alienfx-gui/FanDialog.cpp`, `SysMonHelper.cpp`,
  `alienfx-gui.cpp`, `alienfx-mon.cpp`). Since the project builds `CharacterSet=NotSet`
  (narrow strings), the straightforward move for any file that's staying alive on
  Linux is `#define TCHAR char` + `_T(x) x` — but check each file for direct
  `wchar_t`/`BSTR`/`LPWSTR` use first (the WMI code and the driver loader do use wide
  strings genuinely, not through the `TCHAR` macro — those files aren't being ported at
  all, see [05](05-alienfan-sdk-thermal.md)).
- **Anonymous `struct`-inside-`union`** (MS/C11 extension), 19+ sites concentrated in
  `AlienFX_SDK.h:37-42,65-68,161-166`. GCC and Clang accept this in C++ with a warning;
  either silence the warning (`-Wno-microsoft-anon-tag` on Clang, no direct GCC
  equivalent — GCC treats it as a language extension without a dedicated flag) or
  rewrite the handful of structs to use named members + accessor methods. Prefer
  silencing for now — these are the color/light-mask union types at the heart of the
  HID protocol code (see [04](04-alienfx-sdk-hid.md)); rewriting them risks subtly
  changing packing/alignment.
- **`#define byte BYTE`** at `alienfan-tools/alienfan-SDK_v2/alienfan-SDK.h:11` — this
  file isn't being ported (dead code path on Linux, see
  [05](05-alienfan-sdk-thermal.md)), but if any shared header ever pulls it in
  transitively, it will collide with `std::byte`/`<cstddef>` on GCC/Clang. Don't let
  any Linux-built translation unit include `alienfan-SDK_v2/*` at all.
- **Hardcoded backslash paths** — e.g. `".\\Mappings\\devices.csv"`
  (`alienfx-gui/DevicesDialog.cpp:705`), `L"\\HwAcc.sys"` — replace with
  `std::filesystem::path` construction using `/` (which `std::filesystem` normalizes
  correctly on both platforms) wherever the surrounding file is being ported.
- **MSVC intrinsic macros** (`MAKELPARAM`, `MAKEWORD`, `LOWORD`/`HIWORD`/`LOBYTE`/`HIBYTE`)
  used freely, including inside the SDK's device-ID packing
  (`AlienFX_SDK.cpp` uses `MAKELPARAM(pid, vid)` to build a combined device ID). These
  are trivial bit-twiddling macros — add narrow definitions to `win_compat.h` rather
  than rewriting call sites:
  ```cpp
  #define LOWORD(l) ((WORD)((l) & 0xffff))
  #define HIWORD(l) ((WORD)(((l) >> 16) & 0xffff))
  #define LOBYTE(w) ((BYTE)((w) & 0xff))
  #define HIBYTE(w) ((BYTE)(((w) >> 8) & 0xff))
  #define MAKELPARAM(lo, hi) ((int32_t)(((WORD)(lo)) | (((DWORD)((WORD)(hi))) << 16)))
  ```

## What this doc does not cover

Registry APIs (`RegCreateKeyEx`, `RegGetValue`, etc.) are not a "compat shim" problem —
their Linux replacement is a real design decision about config format and location,
covered in [06](06-configuration-storage.md). Don't try to fake a `HKEY`-shaped API
over files; replace the config classes' internals instead.
