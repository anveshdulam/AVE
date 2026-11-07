# ADR-0003: Versioned Native Project Model and Exact Rational Time

- **Status:** Accepted
- **Date:** 2026-11-06
- **Context:** Non-linear editing requires frame-accurate, sample-accurate cutting without accumulated rounding drift across arbitrary timeline durations and frame rates. The system must support complex multi-track timelines, nested sequences, keyframed parameters, audio buses, and AI proposal transactions.
- **Decision:**
  - Create a versioned, proprietary **native project model** stored in SQLite (WAL mode).
  - Adopt **exact rational arithmetic (`int64_t numerator / int64_t denominator`)** as the sole canonical representation for timeline time, source PTS mapping, and cut boundaries. Never store cut locations as floating-point seconds (`double`).
  - Use **OpenTimelineIO (OTIO)** strictly as an import/export interchange adapter, not as the internal persistent state.
  - All project mutations must be executed through the **Command Bus** as validated, atomic transactions with full undo/redo capability.
- **Alternatives Considered:**
  - *OpenTimelineIO as native model:* OTIO is an editorial interchange format; it lacks representation for proprietary node graphs, audio buses, deep color grades, and internal cache state.
  - *Floating-point seconds (`double`):* Causes cumulative rounding errors, frame-slipping, and A/V sync drift over long timelines.
  - *FFmpeg filter scripts as project format:* Unsuitable for interactive scrubbing, keyframe interpolation, and non-destructive transaction rollback.
- **Consequences:**
  - All engine, UI, and AI components interface with exact `Rational` time types.
  - Every project entity (Track, Clip, Marker, Transition) possesses a stable UUID.
  - AI proposals map directly into transactional command lists.
- **Risks:**
  - Arithmetic overflow on integer multiplications during rational operations. Mitigated by `std::gcd` reduction on every operation and 64-bit integers.
- **Validation Plan:**
  - Comprehensive unit testing in `tests/smoke_test.cpp` and subsequent test suites covering rational math, frame conversions, and edge cases.

