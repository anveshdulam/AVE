# ADR-0004: Five-Layer Architecture and AI Integration Boundary

- **Status:** Accepted
- **Date:** 2026-11-06
- **Context:** An autonomous professional NLE must combine deterministic real-time video/audio playback with AI editing capabilities. Without strict architectural boundaries, NLEs frequently suffer from UI deadlocks, thread contention in audio callbacks, and project corruption caused by unvalidated automation.
- **Decision:**
  - Enforce a strict **Five-Layer Architecture**:
    1. **Presentation Layer:** Qt 6 / QML desktop shell. Emits typed commands, renders frames. Never decodes media or accesses project storage directly.
    2. **Application Core Layer:** Authoritative Project Model, Command Bus, Media Pool, Autosave, and AI Orchestrator.
    3. **Real-Time Engines:** Playback Clock, Frame Scheduler, GPU Render Graph, Color Engine, Audio Engine. Consumes immutable snapshots; never mutates project state.
    4. **Media and Data:** FFmpeg codec adapters, OpenImageIO, caches (proxies, thumbnails, waveforms), SQLite database.
    5. **Platform:** Operating system APIs, Vulkan drivers, audio devices, hardware codecs.
  - Dependencies flow strictly **downward**; status and events flow **upward**.
  - **AI Subsystem Position:** AI resides in the Application Core. It communicates with local worker processes via versioned IPC (Protobuf/named pipes). It outputs typed, bounded `EditPlan` envelopes (`schemas/edit-proposal-envelope.schema.json`) validated by core validators and applied exclusively as Command Bus transactions.
- **Alternatives Considered:**
  - *Direct UI automation / macro scripting by AI:* Fragile, race-condition prone, and un-undoable.
  - *AI as external web service:* Violates the local-first, privacy, and zero-network-dependency constitution.
  - *Monolithic single-layer architecture:* Leads to UI freezes during decoding and high technical debt.
- **Consequences:**
  - Clear separation of concerns and independent testability.
  - Real-time audio callback remains completely lock-free, allocation-free, and I/O-free.
  - AI edits are 100% inspectable, previewable, and undoable by human editors.
- **Risks:**
  - Inter-layer boilerplate. Mitigated by standardized event contracts and clean C++20 DTO interfaces.
- **Validation Plan:**
  - Enforce dependency direction via CMake module boundaries and static architectural reviews.

