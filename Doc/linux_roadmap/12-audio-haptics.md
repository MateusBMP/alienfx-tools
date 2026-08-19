# Audio-Reactive Haptics

## Current implementation

`alienfx-gui/WSAudioIn.h`/`.cpp` (270 lines) captures the system audio output via
WASAPI loopback: `<Audioclient.h>`, `<Mmdeviceapi.h>`,
`CoInitializeEx(COINIT_MULTITHREADED)` (`:26`),
`CoCreateInstance(__uuidof(MMDeviceEnumerator))` → `IMMDeviceEnumerator` (`:126`),
`IMMDevice::Activate(__uuidof(IAudioClient))` (`:89`), `IAudioClient::GetMixFormat`
(`:91,98`), `Initialize(AUDCLNT_SHAREMODE_SHARED)` (`:93`), `SetEventHandle` (`:100`),
`GetService(__uuidof(IAudioCaptureClient))` (`:101`); also uses
`IAudioClockAdjustment` and `ISimpleAudioVolume`. Event-driven capture loop on
`WaitForMultipleObjects(stopEvent, hEvent)` (`:36-38,157,205`).

Captured samples feed the vendored `kiss_fft` library (`kiss_fftr_alloc`/`kiss_fftr`,
`WSAudioIn.cpp:210-224`) for frequency analysis driving the haptic light response
(`HapticsDialog.cpp`, `Haptics-Input` config key — see [06](06-configuration-storage.md)).

## What ports unchanged

**`kiss_fft` needs zero changes.** It's pure C DSP with no Windows dependency (see
`alienfx-gui/kiss_fft/` — `kiss_fft.cpp` 366 lines, `_kiss_fft_guts.h`, `kiss_fft.h`).
It's the one piece of this entire subsystem that's already portable — flag this
explicitly so nobody wastes time "porting" it. The frequency-to-light-response mapping
logic downstream of the FFT (in `HapticsDialog.cpp`) is similarly platform-independent
and should port with minimal changes once real sample data is flowing in from the
Linux capture backend below.

## Linux replacement: PipeWire or PulseAudio monitor capture

- **PipeWire (primary target)**: capture the default output sink's **monitor source**
  via `pw_stream`. Since most current Linux desktops route audio through PipeWire
  (including as a drop-in PulseAudio replacement), this is the primary path to
  implement.
- **PulseAudio fallback**: for systems still running PulseAudio directly (or
  PipeWire's `pipewire-pulse` compatibility layer, which most already have), the
  equivalent is opening a stream against the `<sink-name>.monitor` source via
  `pa_simple`/`pa_stream`. Given PipeWire's pulse-compat layer covers most real-world
  cases, this fallback is lower priority than the pure-PipeWire path — implement only
  if testing shows gaps.
- Both paths deliver PCM frames directly analogous to what `WSAudioIn` hands to
  `kiss_fftr` today — the integration point (buffer → FFT) doesn't change shape, only
  the capture-loop plumbing feeding it does.

## Device enumeration and format negotiation

WASAPI's `GetMixFormat` auto-negotiates the shared-mode format; PipeWire/PulseAudio
require the client to either accept the server's negotiated format (simplest, and
sufficient here since this feature only needs amplitude/frequency-band data, not
lossless fidelity) or explicitly request one. Recommend accepting the server's default
format and resampling/reformatting on the client side only if `kiss_fftr` needs a
specific sample rate the server doesn't offer — avoid over-engineering the negotiation
path for a feature that's inherently approximate (visual haptics, not audio
processing).

## Architecture placement

Like ambient capture ([11](11-ambient-capture.md)), audio-reactive haptics should run
in the daemon (`alienfxd`, [09](09-daemon-and-monitor.md)) so the effect persists with
the GUI closed, consistent with the tray-resident behavior of the Windows tools.

## Priority note

This is a lower-priority milestone than ambient lighting or core light/fan control —
sequence it after [10](10-gui-qt6.md)'s core GUI ships, per
[17](17-milestones.md).
