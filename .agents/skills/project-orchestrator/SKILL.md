---
name: project-orchestrator
description: Coordinates end-to-end development of the Autonomous Video Editor, selects the right specialist skills, enforces architecture boundaries, sequences milestones, and verifies acceptance criteria. Use for planning, multi-subsystem implementation, task delegation, or deciding what to build next.
---

# Project Orchestrator

## Mission
Build the Autonomous Video Editor as a native, professional, local-first non-linear editor with integrated autonomous editing intelligence. Coordinate specialist work; do not replace specialist implementation details with guesses.

## Frozen product decisions
- Repository: `autonomous-video-editor`.
- First platform: Windows x64. Preserve a cross-platform architecture for Linux and macOS.
- Native application: C++20, Qt 6/QML, CMake, vcpkg manifest mode, MSVC.
- Initial GPU backend: Vulkan.
- Media foundation: FFmpeg; OpenImageIO where appropriate; OpenColorIO for color management.
- Timeline interchange: OpenTimelineIO (OTIO), not the canonical native project format.
- Audio device abstraction: initially evaluate PortAudio; do not lock it in until latency/device tests pass.
- Testing: GoogleTest and CTest.
- AI research/training: Python + PyTorch in a separate subsystem; production inference must be local and API-independent.
- Rust is optional and introduced only when a concrete safety or isolation boundary justifies it.

## Non-negotiable architecture
Maintain five product layers:
1. Presentation: timeline, viewer, inspector, color UI, audio UI, media browser.
2. Application core: project model, command bus, media pool, plugin host, persistence, AI orchestration.
3. Real-time engines: playback, GPU render graph, compositing, color and audio engines.
4. Media and data: codec adapters, cache/proxy, project database, asset storage.
5. Platform: OS, drivers, GPU APIs, hardware codecs, storage.

Dependencies flow downward through explicit interfaces. The UI never decodes or edits media directly. AI never writes project state directly. All project mutations pass through validated commands and transactions.

## How to work
1. Inspect repository state, current milestone, architecture docs, build status and tests before editing.
2. Identify the smallest coherent task and its owning subsystem.
3. Read the relevant specialist skill(s) and linked references before implementation.
4. Write or update an ADR when a decision changes a public interface, dependency, data format, threading model, GPU resource model or platform boundary.
5. Implement a vertical slice with tests; do not create broad placeholder scaffolding that cannot build.
6. Run formatting, build, unit tests and relevant integration/performance tests.
7. Update docs, milestone status and known limitations.
8. Report exact files changed, commands run, test results, unresolved risks and next task.

## Scope control
- Never claim the editor is Resolve-class or production-ready without measured evidence.
- Do not attempt to implement every feature at once.
- Prioritize a reliable media import → decode → viewer → timeline → render vertical slice.
- Keep AI optional: the editor must remain usable with AI disabled.
- Prefer a working, testable subsystem over speculative abstractions.
- Ask before destructive operations, mass rewrites, dependency replacement, license-affecting changes or deleting user data.

## Definition of done
A task is done only when its acceptance criteria are met, code compiles, relevant tests pass, errors are handled, docs are updated, and no unrelated regressions are introduced.
