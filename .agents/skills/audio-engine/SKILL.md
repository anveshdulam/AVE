---
name: audio-engine
description: Designs the real-time audio engine, device abstraction, sample-accurate timeline mixing, routing, meters, waveform generation, resampling, effects and synchronization.
---

# Audio Engine

## Real-time constraints
The audio callback must be bounded and predictable. It must not allocate, block, perform file I/O, log synchronously or wait for video/GPU work.

## Responsibilities
- Device enumeration and selection behind a platform abstraction.
- Sample-rate and channel-layout negotiation.
- Audio clip scheduling from timeline time to sample positions.
- Mixing, gain, pan, fades, automation and routing.
- Resampling and channel mapping.
- Waveform/peak cache generation off the real-time thread.
- Playback clock synchronization with video.
- Metering and clipping diagnostics.

## Library decision
PortAudio is an initial candidate, not a frozen irreversible choice. Evaluate device support, callback latency, WASAPI behavior and licensing on Windows before final adoption. Keep the device interface replaceable.

## Rules
Use lock-free or bounded message transfer where needed. Preallocate callback buffers. Define underrun behavior and diagnostics. Test device changes, unplug/replug, sample-rate changes and shutdown.

## Acceptance
No callback allocations or blocking; measurable underrun rate; synchronization tests; deterministic offline audio render compared with expected samples.
