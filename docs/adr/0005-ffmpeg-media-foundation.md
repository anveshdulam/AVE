# ADR 0005: FFmpeg Media Ingest, Demuxing and Codec Integration

## Status
Accepted

## Context
A professional non-linear video editor requires low-level media container probing, demuxing, decoding, and stream metadata extraction across diverse broadcast, professional, and consumer codecs. 

We require:
1. Native C/C++ integration without spawning external subprocesses during timeline operations.
2. Direct access to packet timestamps, rational timebases, stream maps, codec parameters, pixel formats, audio sample formats, and color metadata.
3. Strict compliance with licensing and distribution policies: the editor core is local-first, native, and must avoid proprietary GPL encumbrance where LGPL is required.
4. Clean architectural boundaries: presentation and timeline layers must never call FFmpeg directly. All media container inspection must be wrapped in `media/` subsystem adapters and exposed through immutable `core/` asset representations.

## Decision
1. **FFmpeg Foundation:** Adopt FFmpeg (libavformat, libavcodec, libavutil, libswscale, libswresample) as the Layer 4 Media foundation.
2. **Distribution & Build Target:** Target the LGPL 3.0+ build of FFmpeg 8.1+ with shared libraries (`.dll` on Windows, `.so` on Linux, `.dylib` on macOS), located under `D:\DevTools\ffmpeg` for local developer builds.
3. **Abstraction Layer (`ave_media`):** All FFmpeg C APIs are wrapped within the `media/` subsystem (e.g. `ave::FFmpegProbe`). No raw `AVFormatContext*` or `AVCodecParameters*` leak into `core/` or `apps/`.
4. **Immutable Asset Projection:** Probing produces an immutable `ave::MediaAsset` in `core/` with a stable RFC 4122 v4 UUID, canonical file path, rational timebase, stream array, and complete format details.
5. **Exact Rational Time:** Timestamps and frame durations are recorded strictly as `ave::Rational` (numerator/denominator), avoiding floating-point rounding errors.

## Alternatives Considered
- **GStreamer:** More complex pipeline graph model, higher runtime footprint, less flexible frame-by-frame scrubbing control for NLE timeline engines.
- **DirectShow / Media Foundation (Windows-only):** Non-portable, poor support for professional formats (e.g. ProRes, DNxHD, AV1), difficult frame-accurate seeking semantics.
- **Libav / Custom Demuxers:** Incomplete format support, high maintenance burden.

## Consequences
- **Positive:** Universal format support (H.264, HEVC, ProRes, AV1, AAC, PCM, Opus, VP9), hardware acceleration hooks (VPL, Vulkan Video, OpenCL), robust metadata extraction, fully testable with synthetic test fixtures.
- **Negative:** FFmpeg C API lifecycle management requires rigorous RAII wrappers; DLL runtime distribution must be included in packaging manifests.
- **Licensing Compliance:** Kept LGPL-compliant; GPL-only codecs (e.g. x264/x265 enc) remain optional or modularized.
