---
name: playback-engine
description: Builds the real-time playback subsystem: decode scheduling, frame queues, prefetch, scrubbing, clocking, audio/video synchronization, buffering and dropped-frame diagnostics.
---

# Real-Time Playback Engine

## Core design
Playback is a clock-driven pipeline with asynchronous demux, decode, frame scheduling, compositing and presentation. It must support pause, play, seek, scrub, reverse/step where feasible, and timeline-rate changes.

## Requirements
- Use rational timestamps and stream time bases.
- Separate demux, video decode, audio decode, scheduling and presentation responsibilities.
- Use bounded queues and explicit backpressure.
- Prefetch around the playhead; cancel stale decode work after seeks.
- Define a master clock policy and A/V synchronization behavior.
- Handle VFR, mixed frame rates, gaps, overlaps, speed changes and audio resampling.
- Distinguish preview quality from final render quality.
- Track dropped frames, late frames, decode latency, queue depth and seek latency.
- Never let the audio callback wait on video decode or GPU rendering.

## Scrubbing
Use seek generations/tokens so stale asynchronous frames cannot overwrite newer seek results. Coalesce rapid scrub requests and prioritize the most recent requested time.

## Acceptance
Tests cover play/pause/seek/scrub, VFR, mixed media, long GOP media, audio sync, cancellation, shutdown and low-resource conditions. Performance targets must be defined from measured hardware and project profiles, not invented.
