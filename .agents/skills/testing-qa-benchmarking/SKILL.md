---
name: testing-qa-benchmarking
description: Creates unit, integration, end-to-end, media-corpus, GPU validation, audio synchronization, regression and performance tests for the NLE and AI editing system.
---

# Testing, QA and Benchmarking

## Test pyramid
- Unit tests: project model, time math, commands, schemas, cache keys and parsers.
- Integration tests: FFmpeg adapters, decoder, GPU backend, audio device abstraction and persistence.
- End-to-end tests: import → timeline edit → preview → render → reopen project.
- AI evaluation: unseen videos, plan validity, timing error, beat alignment and human preference.
- Performance tests: startup, import, seek, playback, render throughput, memory and GPU utilization.

## Test assets
Maintain a legally usable, versioned corpus containing CFR/VFR, mixed frame rates, long-GOP, multiple audio layouts, color metadata, corrupt files, alpha, still images and synthetic patterns. Do not commit copyrighted user media.

## Rules
- Every bug fix adds a regression test.
- Tests must be deterministic where possible.
- GPU tests must report adapter, driver, API version and feature set.
- Performance claims require hardware/software configuration and repeatable methodology.
- Keep slow hardware/media tests separately labeled but run them in release validation.
- Track dropped frames, A/V sync drift, render correctness and memory growth.

## Release gates
No milestone is complete with failing tests, unexplained sanitizer/validation errors, unreviewed security issues or undocumented known limitations.
