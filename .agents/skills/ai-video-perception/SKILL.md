---
name: ai-video-perception
description: Builds the fully local video and audio perception pipeline for reference analysis, shot detection, scene understanding, motion, objects, OCR, speech, beats and structured feature extraction.
---

# Local AI Video Perception

## Goal
Convert reference videos and source footage into timestamped, structured evidence that the editing models can consume. Perception is not the same as creative editing judgment.

## Pipeline
1. Probe streams and validate media.
2. Generate a low-resolution analysis proxy.
3. Detect scene/shot boundaries.
4. Sample frames adaptively, retaining exact timestamps.
5. Extract visual embeddings/features and optional object/OCR/motion labels.
6. Analyze audio: speech, silence, beat/onset, tempo estimates and loudness.
7. Align audio/video features on a shared rational timeline.
8. Store versioned feature records and provenance.
9. Produce a compact reference fingerprint and human-readable report.

## Local-only rules
- No cloud inference or external AI API.
- Downloaded model weights are local assets with recorded source, version and license.
- Provide model availability checks and graceful degraded mode.
- Never send media to external services.
- Avoid retaining unnecessary biometric identity data; detect visual content for editing purposes, not identity recognition.
- Clearly mark uncertain estimates and confidence.

## Engineering rules
Use a job queue, cancellation, caching keyed by media hash + model/version + parameters, and reproducible preprocessing. Keep perception model dependencies separate from the native editor. Expose results through versioned local IPC/data contracts.

## Acceptance
Timestamp accuracy, reproducible features, cache invalidation, local-offline operation, cancellation and graceful handling of missing models.
