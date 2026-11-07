# Autonomous Video Editor (AVE)

A Windows-first, cross-platform, local-first professional non-linear video editor with integrated autonomous editing intelligence.

---

## 1. Product Constitution
- **Core Stack:** C++20, Qt 6 / QML, CMake 3.25+, vcpkg manifest mode, MSVC on Windows.
- **Rendering:** Vulkan behind an internal backend-neutral Render Hardware Interface (RHI).
- **Media Engine:** FFmpeg libraries (`libavformat`, `libavcodec`, `libswresample`); OpenImageIO for still image sequences.
- **Color Pipeline:** OpenColorIO with ACES configurations and scene-referred processing.
- **Timeline Interchange:** OpenTimelineIO (OTIO) interchange adapter; native model is canonical, versioned, and rational-time-based.
- **Audio Foundation:** Sample-accurate internal mixing graph with isolated PortAudio device callback.
- **Autonomous Editing AI:** Local-first, privacy-respecting editing intelligence. Operates strictly via validated `EditPlan` proposals committed as undoable Command Bus transactions.

---

## 2. Five-Layer Architecture

```text
1. PRESENTATION        Qt 6 / QML Desktop Shell, Viewer, Inspector, Timeline UI
       ↓ (Typed Commands / Frame Requests)
2. APPLICATION CORE    Project Model, Command Bus, Media Pool, Autosave, AI Orchestrator
       ↓ (Snapshots / Graph Jobs)
3. REAL-TIME ENGINES   Playback Clock, Render Graph, Vulkan Compositor, Color & Audio
       ↓ (Buffers / Demux Requests)
4. MEDIA AND DATA      FFmpeg Codec Adapters, Cache/Proxy Manager, SQLite Project DB
       ↓ (Driver / OS Calls)
5. PLATFORM            OS APIs, Vulkan 1.4 Driver, Audio Devices, Hardware Codecs
```

---

## 3. Quick Start & Prerequisites

See [docs/DEVELOPMENT_SETUP.md](docs/DEVELOPMENT_SETUP.md) for full setup instructions.

### Prerequisites (Windows x64)
- Visual Studio 2022 (MSVC v143 toolset + Windows 11 SDK)
- CMake 3.25+ & Ninja
- LunarG Vulkan SDK
- vcpkg package manager (`VCPKG_ROOT` configured)
- Qt 6.7+ for MSVC 2022 x64

### Build Commands
```powershell
# Configure using CMakePresets and vcpkg manifest mode
cmake --preset x64-debug

# Build targets
cmake --build --preset x64-debug

# Run automated tests
ctest --preset x64-debug
```

---

## 4. Documentation & Architecture Decision Records (ADRs)
- [Architecture & Implementation Plan](Autonomous_Professional_NLE_Architecture_and_Implementation_Plan.md)
- [Engineering Roadmap](docs/ENGINEERING_ROADMAP.md)
- [Project Status](docs/PROJECT_STATUS.md)
- [Dependency Inventory & Licenses](docs/DEPENDENCY_INVENTORY.md)
- [Known Limitations](docs/KNOWN_LIMITATIONS.md)
- [ADR-0001: Platform, Toolchain, and Build System](docs/adr/0001-platform-and-build-system.md)
- [ADR-0002: Vulkan as Initial GPU Rendering Backend](docs/adr/0002-vulkan-backend.md)
- [ADR-0003: Versioned Project Model and Rational Time](docs/adr/0003-native-project-model.md)
- [ADR-0004: Five-Layer Architecture and AI Boundary](docs/adr/0004-layered-architecture.md)

---

## 5. Antigravity Skill Catalogue
This workspace incorporates 27 specialized engineering skills located in `.agents/skills/`:
- `project-orchestrator` — Coordinates end-to-end development, selects specialist skills, enforces architecture boundaries.
- `architecture-and-adr` — Layered architecture, subsystem boundaries, interfaces, data ownership, and ADRs.
- `cpp20-engineering` — Modern C++20 standards, RAII, value semantics, and concurrency guidelines.
- `cmake-vcpkg-build` — Windows-first CMake presets and vcpkg manifest build management.
- `qt6-qml-presentation` — Qt 6 / QML desktop presentation layer, viewer shell, and inspectors.
- `vulkan-rendering` — Vulkan RHI, GPU synchronization, command submission, and shader pipelines.
- `media-ffmpeg-codecs` — Ingest, probing, demuxing, decoding, encoding via FFmpeg.
- `playback-engine` — Real-time playback scheduling, prefetch, and audio/video synchronization.
- `timeline-project-model` — Canonical non-destructive project model, rational timebase, tracks, and clips.
- `command-bus-undo` — Transactional project mutations, validation, undo/redo, and autosave.
- `gpu-render-graph` — Backend-neutral render graph for compositing, transforms, and export.
- `color-management` — OpenColorIO and ACES-compatible color pipeline, LUTs, and scopes.
- `audio-engine` — Real-time audio engine, sample-accurate timeline mixing, and meters.
- `plugin-host-openfx` — Safe extensible effect plugin host and OpenFX integration.
- `ai-video-perception` — Fully local video and audio perception pipeline and feature extraction.
- `ai-model-training` — Specialized editing Transformer models trained from scratch.
- `ai-orchestration-ipc` — Local IPC, task orchestration, and validated proposal application.
- `edit-plan-schema` — Typed, versioned non-destructive `EditPlan` proposal format.
- `testing-qa-benchmarking` — Unit, integration, golden-render, and performance test suites.
- `performance-profiling` — CPU, GPU, memory, and playback dropped-frame profiling.
- `security-privacy-supply-chain` — Threat model, local privacy, and license compliance.
- `documentation-and-handoff` — Technical documentation, ADRs, and milestone reports.
- `phase-zero-bootstrap` — Phase 0 foundation, environment checks, and minimal spike.
- `release-packaging-licensing` — Windows installer layout and license obligations.
- `media-cache-proxy` — Media pool, thumbnail/waveform caches, and proxy generation.
- `timeline-ui-interaction` — Timeline interaction in Qt/QML, trimming, and snapping.
- `ai-feedback-learning` — Consent-based feedback capture and training example curation.
