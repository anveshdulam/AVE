# ADR-0013: Local AI Video Perception Infrastructure, Caching, and Evaluation Harness

## Context
Phase 7 introduces the local video and audio perception pipeline to analyze source footage and reference videos for autonomous editing. Naive implementations suffer from several major pitfalls:
1. Decoding high-resolution 4K/8K media directly into neural networks causes out-of-memory errors and excessive compute load.
2. Background AI analysis competing with real-time timeline playback causes dropped frames and stuttering.
3. Floating-point timestamp errors accumulate across long sequences, leading to audio-video desynchronization.
4. Thresholded events discard raw prediction probabilities, preventing threshold re-tuning without re-running heavy models.
5. Inability to benchmark perception algorithms against exact ground-truth cuts and musical beats without proprietary datasets.

## Decision
We established a robust, local-first perception infrastructure layer (`ave_ai`) implementing the following architecture:

1. **Standardized Analysis Proxies (`ave::AnalysisProxy`)**:
   - Generates low-resolution proxies (e.g. 480x270 25fps video, 22.05kHz mono audio for beat tracking, 16kHz mono audio for speech) tailored for ML models.
   - Decodes media once into cached proxies so subsequent models (TransNetV2, Beat This!, Whisper, SigLIP 2) run with minimal overhead.

2. **Perception Cache & Raw Probabilities (`ave::PerceptionCache`)**:
   - Cache keys are generated deterministically: `SHA256(asset_hash + ":" + analyzer_id + ":" + version + ":" + model_sha + ":" + params)`.
   - Caches both discrete detected events (`PerceptionReport`) and raw continuous probability curves (e.g. frame transition probabilities, spectral flux).
   - Atomic replacement via `<key>.tmp` staging and rename ensures NTFS integrity.

3. **Strict Rational Time Representation (`ave::PerceptionReport`)**:
   - All timestamps and durations in the perception report strictly use `ave::Rational` (64-bit integer numerator/denominator).
   - Floating-point numbers are prohibited across the perception-to-editor boundary.

4. **Priority-Managed Job Scheduler (`ave::PerceptionScheduler`)**:
   - Enforces the core invariant: **"Playback outranks analysis"**.
   - When timeline playback or GPU compositing is active (`set_playback_active(true)`), worker threads yield CPU/GPU execution.
   - Provides priority ordering (`High`, `Normal`, `Low`), cancellation tokens, and progress callbacks (`0.0` to `1.0`).

5. **Synthetic Ground-Truth Eval Harness (`ave::SyntheticEvalHarness`)**:
   - Generates deterministic test video and audio assets using FFmpeg with bit-exact known cuts, dissolves, flash frames, and click-track BPM.
   - Implements standard benchmarks:
     - Cut detection: Precision, Recall, and F1 score evaluated at $\pm 2$ frames tolerance.
     - Gradual transitions (dissolves/fades): temporal Intersection-over-Union (IoU).
     - Beat detection: Precision, Recall, and F-measure evaluated at $\pm 70$ ms tolerance (ISMIR standard).

## Consequences
- Clean separation between model runtime, caching, scheduling, and presentation.
- Zero external network dependencies or third-party cloud APIs.
- Predictable, non-blocking playback performance during background analysis.
- Verified by automated unit and integration suite `ave_perception_infrastructure_test` passing 100%.
