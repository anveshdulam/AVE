---
name: cmake-vcpkg-build
description: Sets up and maintains the Windows-first CMake and vcpkg build, compiler configuration, dependency manifests, presets, CI, packaging and reproducible developer environments.
---

# CMake and vcpkg Build Engineering

## Baseline
- CMake is the build generator and source of truth.
- Use `vcpkg.json` manifest mode with a committed baseline.
- First target: Windows x64 + MSVC 2022 + C++20.
- Keep Qt, Vulkan SDK, FFmpeg and optional libraries behind explicit dependency configuration.
- Add Linux/macOS presets only after the Windows baseline is reproducible.

## Required files
- `CMakeLists.txt` at root and per-module CMake files.
- `CMakePresets.json` with configure/build/test presets.
- `vcpkg.json` and a pinned baseline.
- `.clang-format`, `.editorconfig`, `.gitignore`.
- CI workflow that configures, builds and runs tests.
- `cmake/` helper modules only when they reduce duplication.

## Rules
1. Pin dependency versions or registry baselines; never use floating latest dependencies.
2. Prefer imported CMake targets; do not hardcode machine-specific include/library paths.
3. Keep third-party warnings isolated from project warnings.
4. Do not commit SDK binaries, build directories, local media or model weights.
5. Make optional features explicit through CMake options.
6. Ensure clean configure/build/test works from a fresh checkout.
7. Document required external SDK installation and environment variables.
8. Never silently change compiler, runtime linkage or Qt major/minor requirements.

## Phase 0 acceptance
A fresh Windows machine with documented prerequisites can clone, configure, compile a minimal Qt application and run CTest using documented commands.
