# ADR 0020: Windows Native WASAPI Audio Output Backend and Platform AudioDevice Contract

## Status
Accepted

## Context
Through Phase 10 and M1.4A, AVE's playback and mixing pipeline (`PlaybackSession`, `AudioMixer`, `AudioRingBuffer`) ended in `NullAudioDevice`, which pulled and discarded mixed PCM on a software thread without sending audio to physical Windows speakers or headphones.

Key engineering constraints for enabling real audible timeline playback on Windows:
1. **Windows-First Native Dependencies:** Zero external third-party audio dependencies (e.g., no PortAudio/SDL/OpenAL) — use native Windows WASAPI via Win32 COM (`ole32`, `avrt`, `uuid`) linked directly by `ave_audio`.
2. **UI / Qt STA Thread Isolation:** The Qt 6 GUI thread runs under single-threaded apartment (`COINIT_APARTMENTTHREADED`) semantics. Initializing or invoking WASAPI COM objects (`IMMDeviceEnumerator`, `IAudioClient`, `IAudioRenderClient`) on the GUI thread risks COM apartment mismatches, UI stalls, and audio dropouts.
3. **Real-Time Render Safety:** The hardware render refill loop cannot allocate heap memory, acquire blocking application mutexes, perform file/FFmpeg decoding, or emit blocking Qt signals.
4. **Honest Failure Reporting (Zero Silent Fallback in Production):** When no default audio endpoint exists, a device is unplugged mid-playback (`AUDCLNT_E_DEVICE_INVALIDATED`), or shared-mode initialization fails, production playback MUST surface an explicit unavailable/error status (`available = false`, `AudioDeviceState::Error`, `last_error`) and NEVER silently fall back to `NullAudioDevice` and claim healthy audio.
5. **Deterministic Headless Testing:** All automated CTest suites, CI builds without audio hardware, `--smoke-test`, and `--verify-qml` must run cleanly without speakers using either an explicit `AudioBackendMode::NullForTesting` opt-in or a mockable `IWasapiEndpoint` seam.
6. **Shared-Mode Hardware Format Flexibility:** Windows shared-mode WASAPI audio endpoints frequently run at different sample rates (e.g., 44.1 kHz, 96 kHz), channel counts (mono, stereo, 5.1/7.1), or PCM sample encodings (`Int16`, `Int24`, `Int32` with `wValidBitsPerSample`) than AVE's canonical engine format (48 kHz, 2ch, interleaved `Float32`).

## Decision
1. **`AudioDevice` Lifecycle & Diagnostics Contract (`engines/audio/include/ave/audio_device.hpp`):**
   - Extend `AudioDevice` with explicit lifecycle (`initialize(config)`, `start(callback)`, `pause()`, `resume()`, `flush()`, `stop()`, `shutdown()`), bounded non-blocking push submission (`submit_pcm`), pull `AudioRenderCallback`, `AudioSampleFormat` / `AudioDeviceState` enums, and structured `AudioDeviceDiagnostics` (`available`, `is_physical`, `backend_name`, `device_name`, negotiated engine/hardware rates/channels/formats, `state`, `underrun_count`, `frames_submitted`, `last_error`).
   - Preserve `NullAudioDevice` strictly for `AudioBackendMode::NullForTesting`, headless CI, `--smoke-test`, `--verify-qml`, and software unit tests.
   - Add `ave::create_platform_audio_device()`, returning `std::make_unique<WasapiAudioDevice>()` on `_WIN32` (and `nullptr` elsewhere).

2. **`WasapiAudioDevice` & `IWasapiEndpoint` Seam (`engines/audio/include/ave/wasapi_audio_device.hpp`):**
   - Implement Windows shared-mode WASAPI using `IMMDeviceEnumerator::GetDefaultAudioEndpoint(eRender, eConsole)`, `IAudioClient` (event-driven `AUDCLNT_STREAMFLAGS_EVENTCALLBACK` with automatic timer-period polling fallback if rejected by an endpoint driver), `IAudioRenderClient`, `PKEY_Device_FriendlyName`, and MMCSS `AvSetMmThreadCharacteristicsW(L"Pro Audio", ...)`.
   - **Dedicated COM MTA Worker Thread:** All COM initialization (`CoInitializeEx(nullptr, COINIT_MULTITHREADED)` / `CoUninitialize()`) and endpoint calls execute exclusively on `WasapiAudioDevice`'s internal worker thread. `diagnostics()` copies pre-updated fields under `diag_mutex_` without invoking COM.
   - **Zero-Allocation Render Conversion (`AudioFormatConverter`):** Pre-allocates scratch and history buffers during `initialize()`. Negotiates shared-mode hardware mix format via `IsFormatSupported` / `GetMixFormat` and converts sanitized/clamped `[-1.0, 1.0]` engine Float32 PCM (`NaN`/`±Inf` zeroed) across arbitrary rates (linear phase-continuous interpolation), channel layouts (mono <-> stereo <-> N-channel front L/R routing), and PCM encodings (`Float32`, `Int16`, `Int24`, `Int32` with valid-bit masking).
   - **Device Invalidation Handling:** `AUDCLNT_E_DEVICE_INVALIDATED` (and related hardware errors) tears down the endpoint on the worker thread, marks `available = false`, transitions to `AudioDeviceState::Error`, and populates `last_error` for UI surfacing.

3. **`PlaybackSession` & `PlaybackService` Integration:**
   - `PlaybackSessionConfig` defaults to `AudioBackendMode::AutoPlatformDefault`, instantiating `create_platform_audio_device()` and surfacing explicit unavailability if hardware initialization fails (zero silent fallback to `NullAudioDevice`).
   - `PlaybackSession::seek()` and `PlaybackSession::stop()` flush `audio_device_->flush()` before `audio_ring_buffer_.reset()` so stale pre-seek PCM cannot play out after a jump.
   - `PlaybackService` exposes reactive audio diagnostic properties (`audioOutputStatus`, `audioOutputAvailable`, `audioBackendName`, `audioDeviceName`, `audioSampleRate`, `audioChannels`, `audioSampleFormat`, `audioDeviceState`, `audioUnderrunCount`, `audioLastError`) updated safely across the worker->GUI boundary, surfaced minimally in `AudioMeterView.qml` and `ToolbarView.qml`.

## Consequences
- **Positive:** Native Windows speaker/headphone playback during timeline transport without external dependencies, clean Qt STA / WASAPI MTA COM separation, glitch-safe format adaptation and seek/stop buffer flushing, honest hardware error diagnostics in GUI & `--status`, and 100% hardware-independent CI verification via `IWasapiEndpoint` mocks and `NullForTesting`.
- **Negative:** Exclusive-mode WASAPI and runtime device hot-swap/user endpoint picker are intentionally deferred beyond shared-mode default endpoint support in M2.1A. Known multi-clip audio decoder/mixer limitations (`StreamAudioDecoder`, clip offset/speed mixing in `pump_audio`) remain separate follow-up tasks (`M2.1B+`).

