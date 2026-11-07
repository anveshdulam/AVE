---
name: timeline-project-model
description: Designs the canonical non-destructive project model, tracks, clips, source ranges, rational time, nested sequences, transitions, keyframes, serialization and OpenTimelineIO interchange.
---

# Timeline and Project Model

## Canonical model
Create a versioned native project schema. It owns sequences, tracks, clips, source references, effects, transitions, keyframes, markers, bins and export settings. Media source files remain unchanged.

## Time model
- Represent time with rational values or integer ticks tied to an explicit timebase.
- Define frame-rate as rational numerator/denominator.
- Define audio sample time separately.
- Avoid float seconds as canonical storage.
- Specify rounding and resampling rules for mixed timebases.

## Entity model
Use stable IDs for projects, sequences, tracks, clips, media assets, effects and keyframes. Define explicit source-in/source-out and timeline placement semantics. Define track ordering, compositing, audio routing, gaps and overlaps.

## Interchange
Use OpenTimelineIO for supported editorial interchange. Maintain explicit mapping tables for unsupported native features. Never silently discard effects, color nodes, plugin state or metadata; emit warnings and a loss report.

## Persistence
- Version every serialized project.
- Use atomic save and recovery/autosave strategy.
- Validate before loading.
- Provide migrations between schema versions.
- Keep project metadata separate from media binaries and caches.

## Acceptance
Round-trip tests preserve timing, ordering, references and supported effects; invalid references are detected; schema migration is tested.
