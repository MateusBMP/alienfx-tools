# LightFX Library: `LightFX.dll` → `liblightfx.so`

## Current implementation

`LightFX/` (a `DynamicLibrary` project, 1108 lines total) emulates Dell's official
`LightFX.dll` API on top of `AlienFX_SDK` (built with `NOACPILIGHTS`, i.e. HID-only),
so LightFX/AlienFX-integrated games and applications work without the real Dell DLL
installed.

- **`dllmain.cpp`** (28 lines) is the entire DLL lifecycle contract: `BOOL APIENTRY
  DllMain(HMODULE, DWORD ul_reason_for_call, LPVOID)` handling `DLL_PROCESS_ATTACH`/
  `DLL_PROCESS_DETACH`. It also does IPC with the GUI via a **named kernel event**:
  `OpenEvent(EVENT_MODIFY_STATE, false, "LightFXActive")` +
  `SetEvent`/`ResetEvent`/`CloseHandle` (`:14-24`) — this tells `alienfx-gui` "a
  LightFX client is active, don't fight it for the lights."
- **`LFX2.h`** (438 lines) declares all 22 exports, each `FN_DECLSPEC LFX_RESULT
  STDCALL LFX_*(...)` inside `extern "C"` (`:48-392`):
  `LFX_Initialize, Release, Reset, Update, UpdateDefault, GetNumDevices,
  GetDeviceDescription, GetNumLights, GetLightDescription, GetLightLocation,
  GetLightColor, SetLightColor, Light, SetLightActionColor, SetLightActionColorEx,
  ActionColor, ActionColorEx, SetTiming, GetVersion`. Export macro:
  `#define FN_DECLSPEC __declspec(dllexport)` when `LIGHTFX_EXPORTS` is defined,
  `__declspec(dllimport)` otherwise (`:32-38`).
- **`LightFX.cpp`** (390 lines) internals: a dedicated update thread
  (`HANDLE updateThread, stopQuery, haveNewElement`, `:31`;
  `DWORD WINAPI CLightsProc(LPVOID)`, `:33`; `WaitForMultipleObjects`, `:44`;
  `SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL)`, `:42`), a
  `CustomMutex` (see [03](03-platform-abstraction.md)) guarding shared state, and a
  call to `afx_dev->LoadMappings()` (registry — see [06](06-configuration-storage.md))
  at `:162`.

The client side lives separately, in `alienfx-LFX/` (a `.vcxitems` shared-source
project, not its own build target, 1166 lines): `LFXUtil.cpp` (232 lines) does
`LoadLibrary(_T(LFX_DLL_NAME))` (`:45`) then ~19× `GetProcAddress` (`:49-67`) to bind
the entry points, plus `FreeLibrary`. Only consumed by `alienfx-cli` (`highlevel`
mode, see [07](07-alienfx-cli.md)).

## Port plan

This is a mostly mechanical port once [03](03-platform-abstraction.md) and
[04](04-alienfx-sdk-hid.md) exist, since `LightFX` already builds on the HID-only SDK
variant:

- **`DllMain` → ELF constructor/destructor**: `__attribute__((constructor))`/
  `((destructor))` functions replace `DLL_PROCESS_ATTACH`/`DETACH`. The named-event IPC
  with the GUI needs a real redesign, not a mechanical swap — a named kernel event has
  no Linux equivalent primitive with the same semantics. Options: a named POSIX
  semaphore (`sem_open`), an abstract-namespace Unix domain socket the GUI/daemon
  listens on, or (preferred, for consistency with the rest of this roadmap) a D-Bus
  signal to `alienfxd` ([09](09-daemon-and-monitor.md)) announcing "LightFX client
  active" — this fits the daemon-centric architecture better than reinventing a
  separate IPC primitive just for this one signal.
- **Export macro**: `__declspec(dllexport)`/`dllimport`
  (`LightFX/LFX2.h:32,35`, and the duplicate copy in `alienfx-LFX/LFXConfigurator.h:31,34`) →
  `__attribute__((visibility("default")))` under `_WIN32`-gated `#ifdef`, plus a linker
  version script (`.map`/`.version`) to control the exported symbol set explicitly —
  don't rely on `-fvisibility=default` globally, scope it to just the 22 `LFX_*`
  symbols the way `__declspec(dllexport)` already does per-symbol on Windows.
- **`STDCALL`**: neutralize per [03](03-platform-abstraction.md) — a no-op on x86-64
  System V, but the macro itself must still resolve to nothing rather than
  `__stdcall`.
- **`alienfx-LFX`'s `LoadLibrary`/`GetProcAddress`** (`LFXUtil.cpp:45-67`) →
  `dlopen(path, RTLD_NOW)` / `dlsym`, `FreeLibrary` → `dlclose`. Library name resolution
  (`LFX_DLL_NAME` macro) needs a Linux path convention — standard shared-object search
  path (`liblightfx.so` via the dynamic linker's normal `ld.so` resolution) rather than
  the Windows same-directory-or-`System32` convention.
- **Threading/mutex internals** (`updateThread`, `CustomMutex`) — already covered by
  [03](03-platform-abstraction.md)'s `ThreadHelper`/`CustomMutex` replacements, no
  additional work here beyond applying those.
- **`Mappings::LoadMappings`** (`LightFX.cpp:162`) → the Linux config backend from
  [06](06-configuration-storage.md), same as everywhere else this call appears.

## Priority: last

The real-world value of this component on Linux is low — there is essentially no
LightFX-integrated game or application ecosystem on Linux the way there is on Windows
(LightFX is a Windows gaming peripheral API). Its only current Linux consumer in this
project is `alienfx-cli`'s optional `highlevel` mode, which [07](07-alienfx-cli.md)
already recommends stubbing out until this milestone lands. Sequence this after
everything else in [17](17-milestones.md) — it exists for completeness/parity, not
because anything else depends on it.
