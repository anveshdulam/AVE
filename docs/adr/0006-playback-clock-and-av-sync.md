# ADR 0006: Real-Time Playback Clocking, Bounded Scheduling, and A/V Synchronization

## Status
Accepted

## Context
A professional non-linear video editor (NLE) requires a real-time playback subsystem that can play, pause, seek, scrub, step frame-by-frame, and run in reverse across complex timelines.

Key technical requirements:
1. **Exact Rational Clocking:** Timestamps must be computed using `ave::Rational` without floating-point accumulation drift over long sequences.
2. **Decoupled Presentation & Decoding:** Real-time playback cannot block on demuxing, disk I/O, or software decoding. Presentation operates from pre-buffered queues.
3. **A/V Synchronization Policy:** Audio and video streams must be synchronized against a reliable clock reference. The audio callback must never wait on video decode or GPU presentation.
4. **Responsive Scrubbing & Seek Cancellation:** Rapid user scrubs on the timeline must cancel stale decode tasks via generation/epoch tokens, avoiding backlog and visual rubber-banding.
5. **Bounded Memory Footprint:** Frame queues must enforce backpressure to avoid unbounded memory consumption on high-bitrate media (e.g., 4K ProRes/RAW).

## Decision
1. **Subsystem Placement:** The playback engine lives in Layer 3 (`engines/playback`), exposing `ave::PlaybackClock`, `ave::BoundedFrameQueue`, and `ave::PlaybackController`.
2. **Clock Policy (`ClockPolicy`):**
   - **`AudioMaster`:** When unmuted audio is active, the audio output hardware sample counter serves as the master clock.
   - **`SystemClockMaster`:** Monotonic steady microsecond clock (`std::chrono::steady_clock`) serves as the master when audio is absent, muted, or when scrubbing.
3. **Seek Generation Tokens (`uint64_t seek_generation`):** Every seek or scrub request increments an atomic seek generation token. The scheduler and display discard any decoded frames tagged with an older generation.
4. **Bounded Queue Architecture:** Pre-decoded video frames reside in a thread-safe `BoundedFrameQueue<VideoFrame>` with configurable capacity (default 12 frames), enforcing bounded memory and backpressure on decode threads.
5. **A/V Drift Detection & Correction:**
   - Threshold tolerance: within $\pm 1$ frame duration is considered synchronized.
   - Video behind clock by $> 1$ frame: scheduler drops non-keyframe rendering to catch up.
   - Video ahead of clock: scheduler holds frame presentation until clock reaches frame PTS.
6. **Diagnostics Tracking:** Track displayed frames, dropped frames, late frames, and current queue depth.

## Alternatives Considered
- **Threaded Sleep Loop with `float` Seconds:** Prone to drift, jitter, and non-deterministic playback rates.
- **Unbounded Async Tasks:** Causes out-of-memory errors and sluggish UI response during rapid scrubbing.
- **Tight Audio-Video Coupling in One Thread:** Causes audio stuttering whenever video decoding hits high CPU load.

## Consequences
- **Positive:** Rock-solid frame synchronization, smooth scrubbing response, zero audio stuttering, bounded RAM usage, fully testable without hardware.
- **Negative:** Requires careful multi-threaded synchronization between decode threads, the playback clock, and the presentation queue.

