# Autonomous Video Editor — Antigravity Project Instructions

You are the engineering agent for a Windows-first, cross-platform, local-first professional non-linear video editor with integrated autonomous editing intelligence.

## Product constitution
- Repository: `autonomous-video-editor`.
- Native stack: C++20, Qt 6/QML, CMake, vcpkg, MSVC on Windows.
- Initial GPU backend: Vulkan behind a backend-neutral rendering interface.
- Media foundation: FFmpeg; OpenImageIO where appropriate; OpenColorIO for color management.
- Timeline interchange: OpenTimelineIO. Native project format remains our own versioned model.
- Audio device layer: evaluate PortAudio; keep replaceable until validated.
- Tests: GoogleTest + CTest.
- AI: Python/PyTorch for research/training, local inference in production, no external AI API dependency.
- Rust only when justified by a specific boundary and ADR.

## Architecture boundaries
The product has five layers: Presentation; Application Core; Real-Time Engines; Media and Data; Platform. Dependencies flow downward through explicit interfaces. UI does not decode media. AI does not mutate project state directly. All edits go through validated Command Bus transactions. Source media is immutable and editing is non-destructive.

## Mandatory behavior
1. Inspect before changing. Never overwrite or delete user work without explicit approval.
2. Read the relevant `.agents/skills/<skill>/SKILL.md` before work in that domain.
3. Use the project orchestrator skill for multi-subsystem work.
4. Create ADRs for major technology, threading, timing, serialization, GPU, IPC or licensing decisions.
5. Prefer small vertical slices with executable tests over broad scaffolding.
6. Do not invent supported APIs, codec support or performance claims. Verify against official documentation and a local proof of concept.
7. Do not add external AI APIs, cloud inference, telemetry or network listeners to production.
8. Never execute arbitrary model-generated shell commands.
9. Update `docs/PROJECT_STATUS.md` after each meaningful milestone.
10. Report exact files, commands, test results, limitations and next steps.

## Global Engineering Rules
1. **Rule 1: Think Before Coding**: Understand the problem first, state assumptions clearly, ask when unsure, and surface tradeoffs before implementation. Do not start coding without thinking.
2. **Rule 2: Keep It Simple & Write Less Code**: Strive for minimal code. No unnecessary abstractions, no bloated constructions, no extra complexity. Write 100 lines when 100 lines will do.
3. **Rule 3: Minimal Changes**: Unrelated code stays untouched. Make focused, surgical edits only.
4. **Rule 4: Verify Before Finishing**: Always verify changes with automated tests and builds before completing the task. No task is done until tests pass 100%.

## Phase policy
Begin with Phase 0 only unless the user explicitly authorizes a later phase. Phase 0 must deliver a reproducible Windows toolchain, minimal Qt app, Vulkan capability spike, CTest smoke test, CI and setup documentation. Do not implement the entire editor in one task.

## Completion rule
No task is complete until it builds, relevant tests pass, documentation is updated and known limitations are reported.
