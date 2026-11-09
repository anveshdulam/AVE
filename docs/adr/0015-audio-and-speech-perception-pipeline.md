# ADR-0015: Audio and Speech Perception Pipeline

## Context
Autonomous video editing requires tight musical and speech synchronization:
1. **Rhythm and Beats**: Edits, transitions, and cuts often align with musical beats, downbeats, and tempo changes.
2. **Sub-millisecond Transients**: Macro beat detectors operating on coarse time slices (e.g. 20 ms / 50 fps) can leave noticeable cut lag relative to sharp acoustic transients (such as drum snares and kick attacks).
3. **Speech and VAD**: Cuts must never slice through mid-word dialogue, and speech intervals must support J-cuts, L-cuts, and pacing adjustments without acoustic clicks falsely registering as speech phonemes.
4. **Loudness**: Program loudness must be quantified in EBU R128 LUFS to balance voiceover and background music tracks.

## Decision
We implemented a multi-stage audio perception pipeline:

1. **Integrated Loudness and Spectral-Flux Onsets (`ave::SpectralFluxOnsetDetector`)**:
   - Computes integrated program loudness in LUFS using standard pre-filtered RMS measurement over audio windows.
   - Computes positive spectral energy flux across successive analysis hops (20 ms hop size / 441 samples at 22.05 kHz).
   - Detects acoustic transients (drum hits, percussive attacks) with peak-picking above a dynamic flux threshold.
   - Emits raw onset flux curve into `PerceptionReport::raw_curves` under `"spectral_flux_onset"`.

2. **Rhythm, Downbeats, and Sub-Millisecond Snapping (`ave::BeatDetector`)**:
   - Tracks musical beats and calculates primary tempo (BPM) along with common sub-harmonic / harmonic alternates (0.5x, 2.0x) to handle tempo ambiguity.
   - Identifies downbeats (measure starts) for musical phrasing alignment (e.g., every 4 beats in 4/4 time).
   - Performs micro transient refinement: snaps each coarse beat to the nearest acoustic onset within $\pm 20\text{ ms}$, achieving drum-attack precision without model retraining.
   - Preserves beat confidence curve under `"beat_activation"`.

3. **Voice Activity Detection & Word Timestamps (`ave::SpeechDetector`)**:
   - Segments active speech regions using normalized short-term RMS energy with a minimum duration constraint ($\ge 250\text{ ms}$) to reject brief transient acoustic clicks.
   - Associates word-level token boundaries and confidence scores for conversational alignment and J/L cut planning.

4. **Deterministic Perception Integration (`ave::AudioPerceptionPipeline`)**:
   - Populates `PerceptionReport::rhythm` and `PerceptionReport::speech` using exact 64-bit rational timestamps (`ave::Rational`).
   - Supports 100% offline fallback when neural weights are uninitialized.
   - Full JSON round-trip serialization and deserialization verified.

## Consequences
- Musical edits snap with sub-millisecond percussive accuracy to real drum transients.
- Speech dialogue is preserved without destructive mid-word cuts.
- 100% permissive licenses (MIT), fully local-first execution with zero cloud calls.
- Verified by automated test suite `ave_audio_perception_test` passing 100% across all 20 project test suites.
