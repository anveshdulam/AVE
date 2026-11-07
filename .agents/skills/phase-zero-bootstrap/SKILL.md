---
name: phase-zero-bootstrap
description: Executes Phase 0 setup for the Autonomous Video Editor on Windows, including repository structure, toolchain checks, CMake, vcpkg, Qt, Vulkan SDK, CI, tests and a minimal native application.
---

# Phase 0 Bootstrap

## Goal
Produce a clean, reproducible Windows development foundation. Do not begin implementing the full editor during Phase 0.

## Before changing files
1. Inspect the existing repository and Git status.
2. Detect Windows version, CPU, RAM, GPU model/VRAM, driver, disk space.
3. Detect Visual Studio/MSVC, CMake, Ninja, Git, Python, Qt, Vulkan SDK and FFmpeg.
4. Record actual versions; do not assume installation paths.
5. Identify any existing code and preserve it.

## Tasks
1. Initialize or verify Git repository `autonomous-video-editor`.
2. Create documented top-level folders: `apps/`, `core/`, `engines/`, `media/`, `platform/`, `ai/`, `tests/`, `docs/`, `cmake/`, `third_party/` (only if policy permits).
3. Add CMake presets for MSVC x64 Debug and Release.
4. Add vcpkg manifest with pinned baseline and only essential dependencies.
5. Add formatting, editor config, ignore rules and license inventory.
6. Add a minimal Qt 6 desktop window.
7. Add a Vulkan capability probe and minimal validation-layer triangle or offscreen test; keep this isolated from the full viewer.
8. Add GoogleTest/CTest and one passing smoke test.
9. Add CI for configure/build/test on Windows.
10. Write `docs/DEVELOPMENT_SETUP.md`, `docs/PROJECT_STATUS.md` and initial ADRs.

## Do not do in Phase 0
- Do not build the full timeline UI.
- Do not write a custom decoder.
- Do not implement a complex render graph.
- Do not train models.
- Do not add every planned dependency at once.
- Do not make cross-platform claims before CI validates them.

## Acceptance criteria
- Fresh checkout can configure and build using documented commands.
- Minimal Qt app launches.
- Vulkan probe reports device and capabilities or a clear actionable failure.
- CTest passes.
- CI passes.
- Dependencies and licenses are inventoried.
- Project status and known limitations are documented.
