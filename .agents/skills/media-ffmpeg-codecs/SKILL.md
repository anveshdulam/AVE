---
name: media-ffmpeg-codecs
description: Implements media probing, demuxing, decoding, encoding and codec adapters using FFmpeg, including hardware acceleration, format support, error handling and licensing awareness.
---

# Media and FFmpeg

## Responsibilities
Provide media ingest, metadata, demux/decode/encode adapters and capability reporting. FFmpeg is the codec foundation; it is not the whole NLE render engine.

## Rules
- Wrap FFmpeg types behind project-owned interfaces where practical.
- Track FFmpeg version, enabled components, build configuration and license implications.
- Probe media before import; record streams, codec, profile, dimensions, frame rate, time base, pixel format, color metadata, audio layout and duration when available.
- Treat timestamps as stream time-base values; do not assume constant frame rate.
- Handle variable-frame-rate media, missing timestamps, corrupt packets, truncated files and unsupported codecs.
- Use hardware decode/encode only after capability detection and fallback validation.
- Keep decode work cancellable and isolated from UI/audio real-time threads.
- Respect packet/frame ownership and FFmpeg reference-counting rules.
- Never assume ProRes RAW, camera RAW or proprietary formats are supported by the default FFmpeg build.
- Do not redistribute codecs or SDKs without reviewing license and patent obligations.

## Acceptance
Probe and decode a diverse test corpus; report unsupported media accurately; no crashes on malformed inputs; software fallback works when hardware acceleration is unavailable.
