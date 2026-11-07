# Engineering Roadmap and Gates

## Phase 0 — Foundation
- Environment audit and reproducible toolchain.
- CMake + vcpkg manifest and pinned baseline.
- Minimal Qt app.
- Vulkan capability/interop spike.
- GoogleTest + CTest.
- Windows CI.
- Setup docs, ADRs, license inventory.
**Gate:** clean configure/build/test and launch.

## Phase 1 — Media foundation
- Media probe and asset model.
- FFmpeg demux/decode adapter.
- Thumbnail and waveform jobs.
- Media pool and cache/proxy metadata.
**Gate:** import and inspect a representative test corpus.

## Phase 2 — Playback
- Decode scheduler, bounded queues and seek generations.
- Viewer frame presentation.
- Audio device spike and A/V clock.
**Gate:** stable playback and scrub with measured dropped frames and sync drift.

## Phase 3 — Project and timeline
- Versioned native project model.
- Rational timebase.
- Tracks, clips, gaps, transitions and markers.
- Command bus, undo/redo and autosave.
- Basic timeline UI.
**Gate:** edit, save, close, reopen and undo/redo without timing drift.

## Phase 4 — Rendering
- Backend-neutral render graph.
- Vulkan resource and synchronization model.
- Compositing and basic effects.
- Offline render/export.
**Gate:** reference-image tests and stable export.

## Phase 5 — AI perception and planning
- Local video/audio feature extraction.
- Reference fingerprint.
- EditPlan schema and validator.
- Local AI worker/IPC.
**Gate:** produce a valid proposal on unseen videos without mutating project state.

## Phase 6 — AI execution and QA
- Proposal preview/approval.
- Command-bus translation.
- QA and correction loop.
**Gate:** undoable AI edit from request to rendered output.

## Phase 7 — Professional expansion
- Color grading, scopes, HDR, advanced audio, OpenFX, proxies, multicam, titles, captions, collaboration only when justified.
**Gate:** subsystem-specific compatibility and performance criteria.

## Phase 8 — Custom model maturation
- Human-reviewed dataset, training baselines, specialized models, held-out evaluation, model packaging and versioning.
**Gate:** reproducible improvement over rule-based baseline and human acceptance.
