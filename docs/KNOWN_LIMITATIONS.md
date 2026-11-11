# Known Limitations and Hardware Constraints

**Date:** 2026-11-10  
**Audited Baseline:** `master` @ `85bc618` (Post-Phase-9 Audit; Pre-Phase-10 Implementation)  

This document tracks verified limitations of the development environment, current implementation, and architectural boundaries established by the 2026-11-10 integration audit.

---

## 1. Local Build Prerequisites vs CI Prerequisites

1. **Missing Native Qt 6 C++ SDK (Local Build Blocker):** `Qt6_DIR` is `Qt6_DIR-NOTFOUND` (`C:\Qt` and `D:\Qt` do not exist). `apps/desktop/ave_desktop.exe` compiles without `AVE_ENABLE_QT`, does not call `QQmlApplicationEngine::load()`, and exposes **zero** `Q_OBJECT` / `Q_PROPERTY` services. **Required Local Prerequisite:** Qt **6.8.3 (`msvc2022_64`)** (open-source LGPLv3 compatibility baseline; commercial-only later 6.8.x LTS patches are not assumed).
2. **Active `build/smoke` Bypassed `vcpkg`:** Although `vcpkg.json` declares `gtest` and `nlohmann-json`, `vcpkg_installed/` currently holds metadata only (`GTest_DIR-NOTFOUND` and `nlohmann_json_DIR-NOTFOUND` in `build/smoke/CMakeCache.txt`). All 24 CTest executables currently execute custom `int main()` assertion harnesses rather than GoogleTest suites.
3. **Pre-Merge CI / Release FFmpeg License Mismatch (Not a Local Build Blocker):** Local `D:\DevTools\ffmpeg` (`n8.1.3`) uses **LGPLv3** shared DLLs with verified MSVC `.lib` import libraries and zero external MinGW runtime DLL dependencies (`dumpbin /dependents`), whereas [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) currently downloads `ffmpeg-master-latest-win64-gpl-shared.zip` (**GPL**), violating ADR-0005 until updated before merge/release.

---

## 2. Current Software Implementation Limitations

1. **Disconnected UI Shell:** The 8 QML views (`apps/desktop/qml/*.qml`) run only via the temporary PySide6 mockup script (`apps/desktop/launch_gui.py`) with hardcoded asset/timeline/AI literals and `Math.random()` meters/waveforms. No live connection to `ave_core`, `ave_media`, `ave_playback`, or `ave_ai` exists in the UI yet.
2. **CPU-Only Runtime Compositing:** `PlaybackSession::update()` (`engines/playback/src/playback_session.cpp`) instantiates `vulkan_compositor_` when given a `VulkanDevice`, but `update()` currently executes CPU `RenderGraph::build_for_timeline()` + `graph->execute()` only. `VulkanCompositor` is exercised in `VulkanRenderTest` using synchronous `end_single_time_commands()` waits rather than a double-buffered `VulkanReadbackRing`.
3. **Simulated Audio Output:** `PlaybackSession::init_audio_device()` instantiates `NullAudioDevice` only; physical WASAPI / PortAudio output is deferred beyond Phase 10.
4. **No Cooperative FFmpeg Import Cancellation:** `FFmpegProbe::probe()`, `ThumbnailExtractor`, `WaveformExtractor`, `StreamDecoder`, and `StreamAudioDecoder` call `avformat_open_input` without `AVFormatContext::interrupt_callback`, and async `MediaImportQueue` / `MediaPoolModel` do not exist yet.
5. **Unprotected Concurrent Prefetch & Playback Evaluation (G-08):** `TimelinePrefetcher::worker_loop()` and `PlaybackSession` read live `const Timeline&` and `const MediaPool&` references without snapshot isolation. Phase 10 replaces direct mutable-model references with immutable `PlaybackSnapshot` instances.
6. **Rational Time & Timecode Edge Cases:** `ave::Rational` (`core/include/ave/time.hpp`) currently has unchecked 64-bit intermediate multiplications (`<`, `+`, `-`, `to_frames`), `std::gcd` `INT64_MIN` UB, no MSVC 128-bit intrinsic arithmetic (`_umul128`/`_udiv128`), no `ui_seconds_to_rational_pts_safe()`, and `Timecode::from_string()` invokes `std::sscanf` directly on `std::string_view::data()` while `from_rational()` supports non-drop-frame timecodes only.
7. **In-Process Mock AI / No Neural Weights:** `LanguageSidecar` runs `m_mock_mode = true` in-process (named-pipe transport is a stub), `ModelRuntime` does not execute physical `.onnx` weights in C++, `FingerprintEngine::compute_distance_detailed()` lacks `std::isfinite()` pre-`min`/`max` input guards, and similarity scores are uncalibrated (`ave_desktop.exe` performs deterministic planning only, zero LLM inference).
8. **Deferred End-User Features:** Timeline export/encoding, `QQuickRhiItem`/QRhi external Vulkan zero-copy presentation, physical audio hardware I/O, OCIO/ACES color grading UI, OpenFX plugins, and Windows installer packaging remain deferred post-Phase 10.

---

## 3. Host Hardware Constraints

### A. Integrated GPU (Intel Arc Xe-LPG)
- **No NVIDIA GPU:** No discrete NVIDIA GPU is present; proprietary CUDA, NVENC, and NVDEC APIs are unsupported on this host.
- **Shared Memory & Bounded Preview Budget:** Intel Arc Graphics (Vulkan `1.4.325`) shares system LPDDR5X memory. Phase 10 live readback previews are capped at `<= 1280x720` (`960x540` default), bounding the full live preview chain (GPU output + 2 staging slots + 4-slot `FramePresentationHub` ring [`14.75 MB` hub portion] + `<= 4` in-flight responses + Qt SG textures) to `<= ~47.9 MB` worst-case (`~27.0 MB` at `960x540`) with no 4K or 60 FPS SLA claimed.

### B. System RAM Ceiling (16 GB LPDDR5X)
- Offline neural training (external to the C++ application) is restricted to compact models (5M–30M parameters) operating on precomputed feature sequences rather than uncompressed 4K video tensors.
