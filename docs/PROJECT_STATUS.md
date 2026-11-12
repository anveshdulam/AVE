# Autonomous Video Editor — Project Status

## Project Baseline
- **Milestone:** Phases 0–8 C++ Subsystems + Phase 9 QML Shell Verified; Phase 10 Tasks 10.1–10.9 (G-01..G-09) & Professional NLE M1.1A–M1.4A + **M2.1A (Native WASAPI Audio Output Backend)** Complete; Task 10.10 & Physical Speaker Playback Manual Verification Pending
- **Date:** 2026-11-12
- **Architecture Status:** 5-layer system and ADRs 0001–0020 established. *(Note: [ENGINEERING_ROADMAP.md](ENGINEERING_ROADMAP.md) defines Phases 0–8; Phases 9–10 are post-roadmap milestone designations documented in [PHASE_10_SPECIFICATION.md](PHASE_10_SPECIFICATION.md).)*
- **Hardware Profile (Audited):**
  - CPU: Intel(R) Core(TM) Ultra 5 125H (14 cores: 4P + 8E + 2LPE, 18 logical threads)
  - RAM: 16 GB LPDDR5X (7467 MT/s)
  - GPU: Intel(R) Arc(TM) Graphics (Xe-LPG GT2, integrated, Vulkan API 1.4.325 supported)
  - Discrete NVIDIA GPU: None present (no CUDA / NVENC dependencies)
  - Remote Git Repository: `https://github.com/anveshdulam/AVE.git`
  - Primary Workspace Storage: `D:\anvesh\AVE` (280 GB free)

---

## Progress Tracker

| Milestone / Deliverable | Status | Details |
|---|---|---|
| **Phase 0: Foundation & Engineering Setup** | **COMPLETE** | Toolchain, CMake, presets, ADRs 0001–0004, smoke test, Vulkan probe passing |
| C++ Compiler & SDK | **Complete** | MSVC v143 (v19.44.35229) & Windows 11 SDK (10.0.26100.0) |
| Build Tools & Package Manager | **Complete** | CMake 4.4.4, Ninja 1.13.2, vcpkg bootstrapped at `D:\vcpkg` |
| GPU Architecture & LunarG SDK | **Complete** | Vulkan 1.4.363.0 at `C:\VulkanSDK\1.4.363.0` (Validation layers verified) |
| Exact Rational Arithmetic | **Hardened (G-02)** | `ave::Rational` (`INT64_MIN`-safe `simplify()`, checked 128-bit MSVC `_umul128`/`_udiv128` + `constexpr` arithmetic & `to_frames`), `ui_seconds_to_rational_pts_safe()` (`9` statuses, 240h / $2^{53}$ ceiling) |
| **Phase 1: Media Foundation** | **COMPLETE** | Ingest pipeline, asset model, media pool, thumbnail/waveform caching |
| Immutable Media Asset Model | **Complete** | `ave::MediaAsset`, `ave::SourceStream`, `ave::UUID` in `core/` |
| JSON Serialization / Deserialization | **Complete** | Exact rational serialization and round-trip verification |
| FFmpeg LGPL Integration | **Complete** | FFmpeg 8.1 LGPL shared libraries in `D:\DevTools\ffmpeg` (`cmake/FindFFmpeg.cmake`) |
| Container & Stream Probing | **Cancellable (G-03)** | `ave::FFmpegProbe` (`probe()` + `probe_custom_io()`) with `AVIOInterruptCB` cooperative cancellation & `std::shared_ptr<std::atomic<bool>>` lifetime ordering past `avformat_close_input()` |
| Architecture Decision Record | **Complete** | ADR-0005 (FFmpeg Media Ingest, Demuxing and Codec Integration) |
| Media Pool & Clip Management | **Cancellable / Qt Model (G-03)** | `ave::MediaPool`, `ave::MediaImportQueue` (2 active / 16 pending / 18 capacity, single-terminal CAS), & `ave::desktop::MediaPoolModel` (`QAbstractListModel`, 5 `Q_PROPERTY`s, 11 roles) |
| Thumbnail & Waveform Background Jobs | **Cancellable (G-03)** | `ave::ThumbnailExtractor` & `ave::WaveformExtractor` with `AVIOInterruptCB` cancellation support, plus `ave::MediaCache` |
| Clangd & IDE Diagnostics | **Complete** | `.clangd`, `compile_flags.txt`, and `.vscode/settings.json` configured for C++20 language server with zero diagnostics |
| Canonical Timeline Model | **Complete** | `ave::Timeline`, `ave::Track`, `ave::TimelineItem` with non-destructive blade cutting and JSON roundtrip |
| Phase 1 Exit Gate | **MET** | Ingest, demuxing, decoding, sub-clipping, caching, and timeline model verified with 100% CTest pass |
| **Phase 2: Real-Time Playback & Timeline Engine** | **COMPLETE** | Playback session, clock, A/V sync, audio engine, presentation view models, desktop shell |
| Architecture Decision Record | **Complete** | ADR-0006 (Real-Time Playback Clocking, Bounded Scheduling, and A/V Synchronization) |
| Rational Playback Clock | **Complete** | `ave::PlaybackClock` (monotonic system clock, audio master sync, 1-frame stepping, speed control) |
| Bounded Frame Queue & Backpressure | **Complete** | `ave::BoundedFrameQueue<VideoFrame>` (thread-safe bounded queue with upstream backpressure) |
| Playback Controller & A/V Sync | **Hardened (G-08)** | `ave::PlaybackController` (action evaluation: Present/Hold/Drop/Stale, seek generation tokens, active `snapshot_version` rejection of outdated frames, synchronized timing) |
| Stream Video Decoder | **Cancellable (G-03)** | `ave::StreamDecoder` (frame-accurate keyframe seeking, RGBA scaling via swscale, exact rational PTS, `AVIOInterruptCB` cancellation) |
| Timeline Prefetch Worker | **Snapshot-Isolated (G-08)** | `ave::TimelinePrefetcher` (reads immutable `PlaybackSnapshotPtr` only, `decoder_mutex_` + `AVIOInterruptCB` cancel on stop, closes removed/relinked decoders, tags/drops superseded frames) |
| Real-Time Audio Buffer & Limiter | **Complete** | `ave::AudioBuffer` (pre-allocated multi-channel float buffer, gain, RMS/peak metering, soft limiter) |
| Lock-Free Audio RingBuffer | **Complete** | `ave::AudioRingBuffer` (SPSC lock-free circular buffer, zero-alloc audio callback, underrun silence fill) |
| Multi-Track Audio Mixer | **Complete** | `ave::AudioMixer` (sample-accurate mixing, constant-power panning, track solo/mute routing) |
| Audio Device Abstraction & WASAPI Output | **M2.1A Complete (ADR-0020)** | `ave::AudioDevice` lifecycle (`initialize`, `start`, `pause`, `resume`, `flush`, `stop`, `shutdown`, bounded `submit_pcm`, `AudioDeviceDiagnostics`), zero-alloc `ave::AudioFormatConverter` (Float32/Int16/Int24/Int32, channel mapping, phase-continuous resampling), Windows native `ave::WasapiAudioDevice` + `ave::IWasapiEndpoint` (`ole32`/`avrt`/`uuid`, event-driven + timer fallback, dedicated COM MTA worker thread), `ave::create_platform_audio_device()`, and deterministic `ave::NullAudioDevice` for `NullForTesting`/headless CI |
| Stream Audio Decoder | **Cancellable (G-03)** | `ave::StreamAudioDecoder` (resampling to 32-bit float PCM via libswresample, sample-accurate seeking, `AVIOInterruptCB` cancellation) |
| Playback Session Coordinator | **Snapshot-Isolated (G-08)** | `ave::PlaybackSession`, `ave::PlaybackSnapshotStore`, & `ave::ProjectSnapshotCoordinator` (immutable `PlaybackSnapshotPtr` reads in `update()`/`pump_audio()`, command/media/relink/replace republish, reverse shutdown order) |
| SMPTE Timecode Calculator | **Hardened (G-02)** | `ave::Timecode` (strict 11-char NDF `HH:MM:SS:FF`, bounded copy, `;` drop-frame rejected, checked 128-bit integer division & range checks) |
| Presentation ViewModels | **Complete** | `ave::TimelineViewModel`, `ave::PlaybackViewModel` (decoupled presentation state and transport bar) |
| Desktop Application Shell | **Qt 6.8.3 Complete C++ Service Layer (46/46 Props: G-01..G-07)** | `apps/desktop/ave_desktop.exe` compiles cleanly with required Qt 6.8.3 (`Core Gui Quick Qml QuickControls2`) + `ave_desktop_services` (`MediaPoolModel` 5 props, `PlaybackService` 12 props, `LiveFrameImageProvider("aveframe")`, `TimelineModel` 13 props + `tracksModel`/`clipsModel`, `InspectorService` 10 props, `AiStudioService` 6 props = `46/46` `Q_PROPERTY` declarations), QRC QML resources + `windeployqt` deployment, exposes all 5 context properties (`mediaPoolModel`, `timelineModel`, `inspectorService`, `playbackService`, `aiStudioService`) & binds live decoded/GPU frames & transport to `ViewerView.qml`, loads `qrc:/ave/qml/main.qml` cleanly with deterministic setup/reverse teardown, and launches via `run_editor.bat`; remaining QML view bindings/mock removal (`apps/desktop/qml/`), `#30 Phase10QmlOffscreenSmoke` + CI LGPL FFmpeg update, and manual gate await Tasks 10.8–10.10 |
| End-to-End Playback Integration Test | **Complete** | `ave_playback_integration_test` (synchronous A/V playback, seeking, scrubbing tokens, timecode) |
| Phase 2 Exit Gate | **MET** | Complete real-time video/audio playback pipeline verified with 11/11 automated tests passing 100% |
| **Phase 3: GPU Render Graph & Vulkan Compositor** | **COMPLETE** | Backend-neutral DAG compositing, affine transforms, blend modes, multi-layer timeline builder, Vulkan compute compositor, real-time PlaybackSession integration |
| Architecture Decision Record | **Complete** | ADR-0007 (Backend-Neutral GPU Render Graph and Compositing Pipeline) |
| Architecture Decision Record | **Complete** | ADR-0008 (Vulkan Offscreen Hardware Device and Compute Compositing Pipeline) |
| Architecture Decision Record | **Complete** | ADR-0009 (Real-Time Composited Playback Pipeline and Presentation Integration) |
| Canonical VideoFrame DTO | **Complete** | `ave::VideoFrame` moved to `core/include/ave/video_frame.hpp` for universal inter-subsystem access |
| 2D Image Buffer & Bilinear Sampler | **Complete** | `ave::RenderImage` (RGBA8, RGBA16, RGBA32, Rec709, sRGB, linear Rec709, bilinear sampling) |
| Compositing Node Hierarchy | **Complete** | `ave::RenderNode`, `SourceNode`, `TransformNode`, `OpacityNode`, `BlendNode`, `OutputNode` |
| Directed Acyclic Graph (DAG) Manager | **Complete** | `ave::RenderGraph` (Kahn's topological sort, cycle detection, automatic timeline graph synthesis) |
| Porter-Duff Blend Modes | **Complete** | Alpha Over, Multiply, Screen, Add with exact math and alpha premultiplication handling |
| 2D Affine Transformation Engine | **Complete** | Inverse transformation matrix (translation, scale, rotation around normalized anchor point) |
| Vulkan Hardware Device Context | **Complete** | `ave::VulkanDevice` (Intel Arc GPU selection, compute/graphics queue, command pool, staging commands) |
| GPU Texture & Staging Buffers | **Complete** | `ave::VulkanTexture` (VkImage, VkImageView, memory barriers, bit-exact CPU <-> GPU staging upload & readback) |
| GLSL Compute Shader & Embedded SPIR-V | **Complete** | `compositor.comp` compiled via glslc and embedded in C++ header `ave::shaders::compositor_comp_spv` |
| Vulkan GPU Compositor | **Complete** | `ave::VulkanCompositor` (compute pipeline, descriptor sets, push constants, 16x16 workgroup hardware dispatch) |
| Phase 3 Step 2 Hardware Test | **Complete** | `ave_vulkan_render_test` (Intel Arc GPU execution: bit-exact texture upload/readback, Over blend, solid color alpha) |
| Spatial Properties on TimelineItem | **Complete** | Position ($X, Y$), scale ($X, Y$), rotation, anchor ($X, Y$), opacity, and blend modes with JSON serialization |
| Dynamic Multi-Track Render Graph Builder | **Complete** | `RenderGraph::build_for_timeline` dynamically queries active clips across all unmuted tracks at playhead PTS |
| Real-Time Composited PlaybackSession | **Vulkan Readback Active (G-04)** | `PlaybackSession::update()` executes `VulkanCompositor::record_composite_pass()` + 2-slot `VulkanReadbackRing` (per-slot `VkFence`, zero `vkDeviceWaitIdle`, explicit Rec.709→sRGB preview conversion, bounded `<= 1280x720` / `960x540` default) and publishes `VideoFramePacketPtr` to 4-slot `FramePresentationHub`, with seamless CPU `RenderGraph` fallback on headless/`VK_ERROR_DEVICE_LOST` |
| Presentation Shell Compositing Integration | **Complete** | `PlaybackViewModel` and desktop app shell display hardware accelerator profile and compositing status |
| Multi-Track Compositing Integration Test | **Complete** | `ave_compositing_integration_test` (multi-track PiP compositing, 50% scale, 80% opacity, track muting, seek, timecode) |
| Phase 3 Exit Gate | **MET** | Complete real-time GPU/CPU compositing pipeline verified with 14/14 automated tests passing 100% |
| **Phase 4: Non-Destructive Editing & Command Bus Undo/Redo** | **COMPLETE** | Typed Command hierarchy, CommandHistory, undo/redo stacks, dirty tracking, transaction batching |
| Architecture Decision Record | **Complete** | ADR-0010 (Command Bus, Typed Mutations, and Transactional Undo/Redo Engine) |
| Command Contract & Structured Result | **Complete** | `ave::Command`, `ave::CommandResult`, `ave::CommandStatus`, `ave::CommandOrigin` in `core/include/ave/command.hpp` |
| Clip Insertion & Deletion Commands | **Complete** | `ave::InsertClipCommand` (overlap validation) and `ave::DeleteClipCommand` (snapshot stashing) |
| Clip Movement & Repositioning | **Complete** | `ave::MoveClipCommand` (cross-track or temporal relocation with destination collision avoidance) |
| Non-Destructive Blade Split Command | **Complete** | `ave::SplitClipCommand` (blade cut at PTS; symmetric undo restores single original clip) |
| Visual Transform Mutation Command | **Complete** | `ave::SetTransformCommand` (reversible modification of position, scale, rotation, anchor, opacity, blend mode) |
| Track Mute Command | **Complete** | `ave::SetTrackMuteCommand` (track audio/video mute toggle with reversible state) |
| Composite Macro Transactions | **Complete** | `ave::CompositeCommand` (atomic multi-command grouping with automatic rollback on sub-command failure) |
| Command History & Dirty Tracking | **Complete** | `ave::CommandHistory` (bounded undo/redo stacks, save-point dirty tracking `is_dirty()`, listener notifications) |
| Phase 4 Unit & Integration Test | **Complete** | `ave_command_bus_test` (apply/revert symmetry, transaction rollback, dirty state tracking, listener callbacks) |
| Phase 4 Exit Gate | **MET** | Command Bus engine verified with 15/15 automated tests passing 100% |
| **Phase 5: Project Serialization & OpenTimelineIO Interchange** | **COMPLETE** | Versioned project schema (`.aveproj`), OpenTimelineIO importer/exporter, asset relinking |
| Architecture Decision Record | **Complete** | ADR-0011 (Versioned Native Project Persistence, OpenTimelineIO Interchange, and Asset Relinking) |
| Native Project Document | **Complete** | `ave::Project` (`.aveproj` format, atomic file saving with `.tmp` staging, schema versioning `1.0.0`) |
| OpenTimelineIO Exporter | **Complete** | `ave::OtioInterchange::export_to_json` (`Timeline.1`, `Track.1`, `Clip.1`, `Gap.1`, `ExternalReference.1`) |
| OpenTimelineIO Importer | **Complete** | `ave::OtioInterchange::import_from_json` (reconstructs timeline, tracks, clips, gaps, and asset references) |
| Non-Destructive Loss Reporting | **Complete** | `ave::OtioLossReport` (audits warnings and tracks unsupported features during interchange) |
| Asset Relinker & Offline Media Reconciliation | **Complete** | `ave::AssetRelinker` (offline media detection, explicit path relinking, recursive directory batch matching) |
| Desktop Application System Status | **Complete** | `apps/desktop/ave_desktop.exe` displays full 5-layer system status, GPU profile, timeline, and OTIO export |
| Phase 5 Unit & Integration Test | **Complete** | `ave_project_persistence_test` (round-trip project save/load, OTIO export/import, batch asset relinking) |
| Phase 5 Exit Gate | **MET** | Project persistence and interchange verified with 16/16 automated tests passing 100% |
| **Phase 6: Professional Timeline UI & Native Presentation Shell** | **COMPLETE** | QML multi-track canvas, playhead scrubbing, zoom/pan virtual viewport, clip selection & drag-and-drop |
| Architecture Decision Record | **Complete** | ADR-0012 (Timeline UI Interaction Architecture, Viewport Projection, and Presentation Shell) |
| Virtual Viewport & Projection | **Complete** | `ave::TimelineViewport` (time-to-pixel and pixel-to-time projection, pivot zooming, virtual scrolling, track layout) |
| Magnetic Snapping Engine | **Complete** | `ave::TimelineSnapping` (magnetic alignment to origin, playhead, and clip boundaries with configurable pixel threshold) |
| Presentation Inspector ViewModel | **Complete** | `ave::InspectorViewModel` (property observation and non-destructive `SetTransformCommand` dispatch) |
| Command-Integrated Timeline ViewModel | **Complete** | `ave::TimelineViewModel` (tool modes: Select/Razor/Trim/Slip, Command Bus actions: split, delete, move, mute, undo/redo) |
| Professional QML Desktop Layout | **Complete** | Dark-themed QML workspace (`main.qml`, `ToolbarView.qml`, `TrackHeaderView.qml`, `TimelineCanvas.qml`, `InspectorView.qml`) |
| Phase 6 Unit & Integration Test | **Complete** | `ave_timeline_ui_test` (viewport projection, snapping verification, tool transitions, inspector transforms, undo/redo) |
| Phase 6 Exit Gate | **MET** | Presentation layer and interaction engine verified with 17/17 automated tests passing 100% |
| **Phase 7A: Local Perception Infrastructure & Eval Harness** | **COMPLETE** | Analysis proxy pipeline, content-keyed perception cache, priority job scheduler, and synthetic ground truth harness |
| Architecture Decision Record | **Complete** | ADR-0013 (Local AI Video Perception Infrastructure, Caching, and Evaluation Harness) |
| Standardized Analysis Proxies | **Complete** | `ave::AnalysisProxy` (low-resolution 270p video and 22.05kHz mono audio proxies, command builders, mock generation) |
| Perception Cache & Raw Probabilities | **Complete** | `ave::PerceptionCache` (deterministic provenance keying, atomic `.tmp` file saving, raw curve persistence) |
| Canonical Perception Report Schema | **Complete** | `ave::PerceptionReport` (strict `ave::Rational` timestamps, shot boundaries, rhythm, speech, visual, raw curves) |
| Playback-Priority Job Scheduler | **Complete** | `ave::PerceptionScheduler` (priority worker queue, cancel tokens, progress tracking, "playback outranks analysis" throttle) |
| Synthetic Eval Harness | **Complete** | `ave::SyntheticEvalHarness` (FFmpeg ground truth generation, Cut F1 @ $\pm 2$ frames, transition IoU, Beat F-measure @ $\pm 70$ ms) |
| Phase 7A Unit & Integration Test | **Complete** | `ave_perception_infrastructure_test` (proxy, report roundtrip, cache atomic save, scheduler priority, eval metrics) |
| Phase 7A Exit Gate | **MET** | Perception infrastructure verified with 18/18 automated tests passing 100% |
| **Phase 7B: Shot & Transition Detection** | **COMPLETE** | TransNetV2 integration, classical color histogram/ECR fallback, range dissolve fusion |
| Architecture Decision Record | **Complete** | ADR-0014 (Shot Boundary Detection with TransNetV2 and Classical Refinement) |
| Classical Shot & Transition Baseline | **Complete** | `ave::ClassicalShotDetector` (4096-bin Chi-square histogram, Sobel Edge Change Ratio, cut & dissolve interval detection) |
| Model Execution Abstraction | **Complete** | `ave::ModelRuntime` / `ave::IModelRunner` (static shapes, replaceable execution providers, mock and CPU runners) |
| TransNetV2 Deep Learning Detector | **Complete** | `ave::TransNetV2Detector` (rolling 100-frame window, 50-frame overlap stride, max-probability blending, keyframe selection) |
| Unified Fusion & Refinement | **Complete** | `ave::ShotDetector` (hybrid TransNetV2 with +/-2 frame classical contrast snapping, auto fallback, raw probability curve) |
| Phase 7B Unit & Integration Test | **Complete** | `ave_shot_detector_test` (histogram/ECR, model manifest, rolling window, fusion refinement, SyntheticEvalHarness) |
| Phase 7B Exit Gate | **MET** | Shot and transition detector verified with 19/19 automated tests passing 100% |
| **Phase 7C: Audio & Speech Perception** | **COMPLETE** | Beat This! rhythm tracker, tempo curve/downbeats, spectral flux onsets, Silero VAD + whisper tokens |
| Architecture Decision Record | **Complete** | ADR-0015 (Audio and Speech Perception Pipeline) |
| Spectral Flux & Loudness Engine | **Complete** | `ave::SpectralFluxOnsetDetector` (EBU R128 LUFS loudness, 20ms hop size, dynamic peak picking) |
| Beat & Downbeat Tracker | **Complete** | `ave::BeatDetector` (BPM estimation, alternate candidate tempos, downbeat detection, transient snapping) |
| Voice Activity & Word Timestamps | **Complete** | `ave::SpeechDetector` (minimum duration speech filtering, word-level token boundaries) |
| Unified Audio Perception Pipeline | **Complete** | `ave::AudioPerceptionPipeline` (rhythm, downbeats, speech intervals, raw flux curves in PerceptionReport) |
| Phase 7C Unit & Integration Test | **Complete** | `ave_audio_perception_test` (loudness, transients, beat tracking, SyntheticEvalHarness beat F1, VAD, report roundtrip) |
| Phase 7C Exit Gate | **MET** | Audio & speech perception verified with 20/20 automated tests passing 100% |
| **Phase 7D: Visual Perception & Search** | **COMPLETE** | Motion energy, Lab palette/contrast, SigLIP 2 keyframe embeddings, cosine vector search index |
| Architecture Decision Record | **Complete** | ADR-0016 (Visual Perception, Keyframe Embeddings, and Vector Search Index) |
| Color Science & Contrast Analyzer | **Complete** | `ave::ColorLookAnalyzer` (sRGB to D65 XYZ to CIE L*a*b*, mean L/a/b, standard deviation contrast) |
| Inter-Frame Motion Analyzer | **Complete** | `ave::MotionAnalyzer` (luminance displacement, motion curves in PerceptionReport) |
| Semantic Keyframe Embeddings | **Complete** | `ave::SigLIPEmbeddingExtractor` (1152-dimensional L2-normalized vectors for representative keyframes) |
| In-Memory Cosine Vector Index | **Complete** | `ave::VectorSearchIndex` (top-K nearest keyframe search, cosine similarity ranking) |
| Unified Visual Perception Pipeline | **Complete** | `ave::VisualPerceptionPipeline` (color, motion curve, keyframe indexing, and PerceptionReport integration) |
| Phase 7D Unit & Integration Test | **Complete** | `ave_visual_perception_test` (color look, motion energy, 1152-d normalization, nearest-neighbor retrieval) |
| Phase 7D Exit Gate | **MET** | Visual perception and search index verified with 21/21 automated tests passing 100% |
| **Phase 7E: Reference Fingerprint v2** | **COMPLETE** | Multi-dimensional editing distribution (pacing, cut-to-beat, motion, color) & mathematical distance metric |
| Architecture Decision Record | **Complete** | ADR-0017 (Reference Fingerprint v2 and Mathematical Distance Metric) |
| Multi-Dimensional Pacing Distribution | **Complete** | `ShotLengthDistribution` (median, Q1/Q3 percentiles, IQR, burstiness parameter in [-1.0, 1.0]) |
| Cut-to-Beat Phase Distribution | **Complete** | `CutToBeatDistribution` (beat sync ratio within +/-70ms, downbeat ratio, signed ms offset) |
| Transition Mix & Motion Variance | **Complete** | `TransitionMix` (hard cut, dissolve, fade proportions) & `MotionProfile` (mean energy and variance) |
| Universal Dual Extractor | **Complete** | `FingerprintEngine::from_perception_report` & `FingerprintEngine::from_timeline` |
| Normalized Distance Function | **Complete** | `FingerprintEngine::compute_distance` (D(F1, F2) in [0.0, 1.0], metric identity, symmetry, bounds) |
| Detailed Distance Breakdown | **Complete** | `FingerprintEngine::compute_distance_detailed` (pacing, rhythm, transition, motion, color distances) |
| Phase 7E Unit & Integration Test | **Complete** | `ave_fingerprint_test` (pacing, burstiness, beat phase, metric identity/symmetry, timeline extraction, JSON) |
| Phase 7E Exit Gate | **MET** | Reference Fingerprint v2 verified with 22/22 automated tests passing 100% |
| **Phase 7F: Deterministic Planner & Hardened Bridge** | **COMPLETE** | Dynamic programming / beat-grid solver, proposal validator, single-transaction CompositeCommand commit/undo |
| Architecture Decision Record | **Complete** | ADR-0018 (Deterministic Beat-Grid Planner and Hardened Proposal Bridge) |
| Edit Proposal Envelope Schema Model | **Complete** | `ave::EditProposalEnvelope` & `ave::EditOperation` conforming to `edit-proposal-envelope.schema.json` |
| Deterministic Beat-Grid Planner | **Complete** | `ave::DeterministicPlanner` (musical beat-grid cuts, downbeat weighting, candidate scoring, repeat penalty) |
| Hardened Semantic Validator | **Complete** | `ave::ProposalBridge::validate` (boundary enforcement: floats stop at boundary, exact Rational timestamps) |
| Cloned Sandbox Dry-Run Engine | **Complete** | `ave::ProposalBridge::dry_run` (simulates edits on cloned timeline, evaluates candidate distance score D(F_ref, F_cand)) |
| Single Atomic Composite Transaction | **Complete** | `ave::ProposalBridge::commit` (wraps operations into single `CompositeCommand`, exactly 1-step undo/redo) |
| Phase 7F Unit & Integration Test | **Complete** | `ave_planner_test` (beat grid planning, dry-run simulation, atomic composite commit, 1-step undo/redo, diff summary) |
| Phase 7F Exit Gate | **MET** | Deterministic planner and proposal bridge verified with 23/23 automated tests passing 100% |
| **Phase 7: Reference Perception & AI Editing** | **COMPLETE** | 7A (Infra/Eval), 7B (Shots/TransNetV2), 7C (Audio/Beat This!), 7D (Visual/SigLIP 2), 7E (Fingerprint v2), 7F (Planner/Bridge) |
| **Phase 8: Local Language Layer & Interactive AI Editing UI** | **MOCK-BACKED** | In-process mock sidecar (`m_mock_mode = true`), deterministic C++ constraint parser, and C++ proposal inspector |
| Architecture Decision Record | **Complete** | ADR-0019 (Local Language Sidecar, Prompt Constraint Parser, and Interactive Proposal Inspector) |
| Out-of-Process Sidecar Manager | **In-Process Mock** | `ave::LanguageSidecar` operates with `m_mock_mode = true`; named-pipe worker spawn/IPC is currently uninitialized |
| Natural Language Prompt Parser | **Complete** | `ave::PromptParser` (regex/keyword rule-based parser for duration, pacing intent, beat sync, energy, speech preservation) |
| Interactive Proposal Review Inspector | **C++ Core Only** | `ave::ProposalInspector` (plain C++ ViewModel; QML `QObject` service deferred to Phase 10) |
| Phase 8 Unit & Integration Test | **Complete** | `ave_language_layer_test` (simulated crash flag, prompt constraint mapping, proposal inspector, end-to-end prompt to timeline commit) |
| Phase 8 Exit Gate | **MET (Mock Sidecar)** | Language layer and interactive proposal review verified with 24/24 CTest executables passing |
| **Phase 9: Complete Professional UI Overhaul** | **VISUAL PROTOTYPE ONLY** | Unbound PySide6/QML visual shell (`apps/desktop/launch_gui.py`, 8 `.qml` files using static literals and `Math.random()` timers; zero C++ engine linkage) |
| Studio Palette & Dark Theme | **Visual Prototype** | Deep studio zinc/charcoal (`#0f0f13`, `#121215`, `#16161a`), 1px borders, high-contrast SMPTE cyan and emerald accents |
| Top Studio Menu & Workspace Switcher | **Visual Prototype** | `ToolbarView.qml` (hardcoded GPU text & static `"01:00:14:18"` timecode) |
| Studio Media Pool & Bins | **Visual Prototype** | `MediaPoolView.qml` (4 hardcoded mock assets; unbound to C++ `MediaPool`) |
| Broadcast Program Monitor | **Visual Prototype** | `ViewerView.qml` (static gradient backdrop; no decoded frames) |
| Precision Timeline & Waveforms | **Visual Prototype** | `TimelineCanvas.qml` (hardcoded clips & `Math.random()` waveforms) |
| Stereo Hardware Audio Meter | **Visual Prototype** | `AudioMeterView.qml` (`Timer` + `Math.random()` levels; unbound to C++ audio engine) |
| Inspector & AI Prompt Studio | **Visual Prototype** | `InspectorView.qml` (hardcoded `"95.2% Match"` & static SceneA–D entries) |
| Phase 9 Exit Gate | **VISUAL MOCKUP ONLY** | Verified visually under `PySide6 6.12.0` (`launch_gui.py`); native C++/Qt 6 integration specified in [PHASE_10_SPECIFICATION.md](PHASE_10_SPECIFICATION.md) |
| **Phase 10: Native Application Integration** | **IN PROGRESS (10.1–10.9 / G-01..G-09 Complete)** | Day 0 (`Qt 6.8.3 msvc2022_64`, `vcpkg` `gtest`/`nlohmann-json`, `FFmpeg n8.1.3 LGPLv3`), **Task 10.1 (G-01)** native Qt 6.8.3 bootstrap, **Task 10.2 (G-02)** checked 128-bit `Rational` + `ui_seconds_to_rational_pts_safe()` + NDF `Timecode` hardening, **Task 10.3 (G-03)** `AVIOInterruptCB` cooperative FFmpeg cancellation + bounded `MediaImportQueue` (`2/16/18`) + `MediaPoolModel` (`5` properties), **Task 10.4 (G-08)** immutable `PlaybackSnapshot` / `PlaybackSnapshotStore` / `ProjectSnapshotCoordinator` + `PlaybackSession`/`TimelinePrefetcher` isolation, **Task 10.5 (G-04)** `VulkanReadbackRing` (2-slot staging/fence) + `VulkanCompositor::record_composite_pass()` + `VideoFramePacket` + 4-slot `FramePresentationHub` + `LiveFrameImageProvider("aveframe")` + `PlaybackService` (`12` properties), **Task 10.6 (G-05, G-06)** `TimelineModel` (`13` properties, `tracksModel`/`clipsModel` + `MediaCache` waveforms, `Tool` enum `0..3`, `.aveproj` open/save, OTIO export, `AssetRelinker`) + `InspectorService` (`10` properties, `SetTransformCommand`, `< 1e-4` slider loop suppression, single-step `resetTransforms()`), **Task 10.7 (G-07)** `FingerprintEngine` `NaN`/`±Inf`/weight pre-`min`/`max` guards + `ProposalBridge` hardening + `AiStudioService` (`6` properties, single background AI worker consuming immutable `PlaybackSnapshotPtr` + `AVIOInterruptCB`, `std::optional<double>` / null `QVariant` similarity, Spec `3.3 #3` formatted `proposedOperations`, `AiStudioServiceTestAccessor` friend seam under `AVE_BUILD_TESTS`, 1-step `CompositeCommand` commit & reverse rollback), **Task 10.8 Stages 1–5** QML audit + live native bindings across all 8 views (`AudioMeterView`, `ViewerView`, `MediaPoolView`, `ToolbarView`, `TimelineCanvas`, `TrackHeaderView`, `InspectorView`, `main.qml`), and **Task 10.9 (G-09)** (`#30 Phase10QmlOffscreenSmoke` executing `ave_desktop --verify-qml` under `QT_QPA_PLATFORM=offscreen`, `cmake/FindFFmpeg.cmake` explicit `NO_DEFAULT_PATH` isolation, & `.github/workflows/ci.yml` LGPL FFmpeg `-buildconf`/`dumpbin` checks + Qt `6.8.3` + `vcpkg` toolchain/cache + `30/30` CTest assertion) complete (`30/30` CTest entries passing: `49` GTest cases across `6` GTest suites + `23` standalone C++ harnesses + `#30 Phase10QmlOffscreenSmoke`). Task **10.10** (16-step manual native GUI gate) remains pending per [PHASE_10_SPECIFICATION.md](PHASE_10_SPECIFICATION.md) |

---

## Test Verification Summary
All **30** registered CTest entries pass across `x64-debug` (`ctest --preset x64-debug --output-on-failure`, `41.19s`) and `x64-release` (`ctest --preset x64-release --output-on-failure`, `15.67s`, `30/30` passed on `2026-11-12`, MSVC `19.44.35229`, `vcpkg` enabled): `SmokeTest` (`ave_smoke_test`) runs **3/3 GoogleTest cases** (`TimeTest.*`), `Phase10TimeTest` (`ave_phase10_time_test`) runs **14/14 GoogleTest cases** (`Phase10TimeTest.*`), `Phase10ImportTest` (`ave_phase10_import_test`) runs **14/14 GoogleTest cases** (`Phase10ImportTest.*`, including `CompletedImportRepublishesSnapshotForPlaybackSession`), `Phase10FrameProviderTest` (`ave_phase10_frame_provider_test`) runs **7/7 GoogleTest cases** (`Phase10FrameProviderTest.*`, including real **Intel(R) Arc(TM) Graphics Vulkan 1.4.325** readback + `VK_ERROR_DEVICE_LOST` fallback and live `QQmlEngine` `Image` loading under `QT_QPA_PLATFORM=offscreen`), `Phase10ServicesPropertyTest` (`ave_phase10_services_property_test`) runs **6/6 GoogleTest cases** (`Phase10ServicesPropertyTest.*`, verifying all **55 `Q_PROPERTY` declarations** across the 5 core services including **21** on `PlaybackService`, `TimelineModel::Tool` enum `0..3`, real waveforms, snapping/commands, inspector slider loop suppression, `.aveproj`/OTIO/`AssetRelinker`, concurrent `PlaybackSnapshot` safety, M1.3A–M1.3C2 dirty/lifecycle/recovery flows, and M2.1A WASAPI diagnostics/COM isolation/reverse shutdown), `Phase10AiStudioTest` (`ave_phase10_ai_studio_test`) runs **6/6 GoogleTest cases** (`Phase10AiStudioTest.*`, total **50/50 GoogleTest cases** across 6 GTest binaries), **23 standalone C++ harnesses**, and **`#30 Phase10QmlOffscreenSmoke`** (`ave_desktop.exe --verify-qml` under `QT_QPA_PLATFORM=offscreen`, `QT_QUICK_BACKEND=software`, verifying `qrc:/ave/qml/main.qml`, `ApplicationWindow` `1440x900`, `0` QML warnings/errors, and clean reverse shutdown). Repeat stress (`AudioEngineTest` `5/5`, `PlaybackIntegrationTest` `5/5`, plus earlier `Phase10AiStudioTest` & `Phase10ServicesPropertyTest` `5/5`) passed 100%, and `ave_desktop.exe --status` (`WASAPI (Speakers (2- Realtek(R) Audio) | 48000 Hz, 2ch, float32 | Stopped)`), `--smoke-test` (`Composited=true GpuReadback=true HubSeq=1`), and standalone `--verify-qml` exit cleanly with status `0`:
1. `VulkanProbeTest` — Physical GPU enumeration (Intel Arc Graphics 0x7d55, Vulkan 1.4.325)
2. `SmokeTest` — Canonical rational time arithmetic & frame conversion (3 GoogleTest cases)
3. `Phase10TimeTest` — Checked 128-bit `_umul128`/`_udiv128` + `constexpr` parity, `INT64_MIN` & operator overflow guards, all 9 `TimeConversionStatus` paths, 24 fps half-frame boundaries, `30000/1001` 240h ceiling, NDF `Timecode` strict parsing & `;` rejection (14 GoogleTest cases)
4. `MediaAssetTest` — UUID generation, stream mapping, JSON serialization round-trip
5. `MediaPoolTest` — Bin hierarchies, non-destructive sub-clipping, clip moving, case-insensitive search
6. `TimelineTest` — Multi-track timeline, item placement, overlap prevention, active item query, blade cut, transform/opacity serialization, and JSON roundtrip
7. `CommandBusTest` — Command contract, typed commands (Insert, Delete, Move, Split, Transform, Mute), CompositeCommand rollback, undo/redo stacks, and dirty tracking
8. `ProjectPersistenceTest` — Native `.aveproj` atomic save/load, OpenTimelineIO export/import roundtrip, and batch asset relinking
9. `TimelineUiTest` — Viewport projection (with `ui_seconds_to_rational_pts_safe` NaN/`±Inf`/`30000/1001` safety checks), zoom pivot, magnetic snapping, tool switching, inspector transforms, and UI undo/redo
10. `PerceptionInfrastructureTest` — Analysis proxy generation, PerceptionReport JSON roundtrip, atomic PerceptionCache, PerceptionScheduler priority/cancellation, and SyntheticEvalHarness metrics
11. `ShotDetectorTest` — Color histogram & ECR deltas, TransNetV2 rolling window, frame-accurate cut refinement, dissolve ranges, keyframe selection, and SyntheticEvalHarness evaluation
12. `AudioPerceptionTest` — Spectral flux transient detection, EBU R128 LUFS loudness, Beat This! rhythm/downbeats, sub-millisecond transient snapping, Silero VAD segmentation, and word token timestamps
13. `VisualPerceptionTest` — CIE L*a*b* color look, luminance contrast, inter-frame motion energy, SigLIP 2 1152-d embedding extraction, and cosine vector search index
14. `FingerprintTest` — Multi-dimensional editing style distribution, burstiness, cut-to-beat phase, mathematical distance metric D(F1, F2), timeline extraction, and JSON roundtrip
15. `PlannerTest` — Deterministic beat-grid planner, clip selection costs, dry-run simulation, single CompositeCommand commit, and 1-step undo/redo
16. `LanguageLayerTest` — Out-of-process sidecar crash isolation, prompt constraint parsing, proposal inspector ViewModel, and end-to-end user prompt to timeline execution
17. `PlaybackEngineTest` — Exact frame stepping, playback clock, bounded queue backpressure, A/V sync action evaluation, seek generation drain, `PlaybackSnapshotStore` monotonic versioning & retained snapshot immutability, listener ID management, and stale `snapshot_version` rejection
18. `StreamDecoderTest` — Frame-accurate video decoding, sequential PTS advancing, seeking, snapshot-backed `TimelinePrefetcher` worker buffering, and stale-snapshot drain under live mutation
19. `PlaybackIntegrationTest` — End-to-end synchronized A/V playback via `Project` + `PlaybackSnapshotStore` + `ProjectSnapshotCoordinator`, audio pump, scrubbing tokens, SMPTE timecode, `mark_clean()` non-republish invariant, multi-thread concurrent write + prefetch + reader stress, asset relinking, `replace_project()` structural reset, and M2.1A mock WASAPI lifecycle, seek stale-audio flush & non-silent error reporting
20. `CompositingIntegrationTest` — End-to-end multi-track picture-in-picture compositing, spatial transform, opacity, command-driven track mute + undo via immutable snapshots, seeking, and frame accuracy
21. `RenderGraphTest` — DAG topological sort, cycle detection, affine transformation, bilinear sampling, opacity modulation, blend modes (Over, Multiply, Screen, Add), and timeline snapshot compositing
22. `VulkanRenderTest` — Intel Arc GPU hardware execution, bit-exact texture staging upload/download, translation, Porter-Duff Over compositing, and solid color alpha generation
23. `AudioEngineTest` — Audio buffer metrics, lock-free SPSC ringbuffer, panning/solo/mute multi-track mixing, `NullAudioDevice` & `WasapiAudioDevice` lifecycle (init/start/pause/resume/flush/stop/shutdown, `IWasapiEndpoint` mock + real Windows WASAPI hardware probe), zero-alloc `AudioFormatConverter` (Float32/Int16/Int24/Int32, channels, phase-continuous resampling), sanitization/clamping, bounded queue, underruns, `AUDCLNT_E_DEVICE_INVALIDATED` handling, and dedicated COM MTA thread verification
24. `FFmpegProbeTest` — Media container demuxing, video (mpeg4/yuv420p) & audio (aac/44.1kHz) stream metadata extraction
25. `MediaCacheTest` — Video thumbnail frame decoding (160x90 RGBA) & audio waveform envelope extraction (100 peaks/sec)
26. `Phase10ImportTest` — `MediaImportQueue` bounds (`2` active / `16` pending / `18` max, immediate 19th rejection), single-terminal CAS cancel races, `AVIOInterruptCB` custom I/O mid-demux abort & lifetime ordering, extractor/decoder cancellation, `MediaPoolModel` 5 `Q_PROPERTY`s & 11 roles, safe mid-import destruction + stale queued event guard, real fixture import, and `MediaPoolModel::projectMediaMutated` -> `ProjectSnapshotCoordinator` -> `PlaybackSession` integration (14 GoogleTest cases)
27. `Phase10FrameProviderTest` — `VideoFramePacket` overflow-safe stride/size/Rec.709→sRGB contract validation, 4-slot `FramePresentationHub` eviction & zero silent mismatch, `VideoFrameTextureFactory` `Straight`/`Premultiplied` & lock-free `textureSize()`, `LiveFrameImageProvider` async `QSignalSpy`, `Error`, `cancel()` emitting `finished()` last, `max_in_flight = 4` burst coalescing & live `QQmlEngine` `Image` integration, `VulkanReadbackRing` `nonCoherentAtomSize` alignment + real **Intel(R) Arc(TM) Graphics (Vulkan 1.4.325)** 2-slot ping-pong readback, resize & `VK_ERROR_DEVICE_LOST` CPU fallback, and `PlaybackService` 12-`Q_PROPERTY` MetaObject, NaN/bounds clamping & async playback flow (7 GoogleTest cases)
28. `Phase10ServicesPropertyTest` — `QMetaObject` reflection of all 46 implemented `Q_PROPERTY`s (`12+5+13+10+6`), 0-arg `NOTIFY` signals, `TimelineModel::Tool` enum (`0..3`), `tracksModel`/`clipsModel` roles with real `MediaCache` waveforms, snapping ON/OFF & CommandBus undo/redo, `InspectorService` `SetTransformCommand`, `< 1e-4` slider loop suppression, 1-step `resetTransforms()`, safe listener teardown, `.aveproj` atomic open/save, RFC 8259 OTIO export validation, `AssetRelinker` snapshot republish, and concurrent `PlaybackSnapshot` reader safety (5 GoogleTest cases)
29. `Phase10AiStudioTest` — `AiStudioService` 6-`Q_PROPERTY` / zero-fake-setter MetaObject contract & initial null `QVariant` state, `FingerprintEngine` `NaN`/`±Inf`/weight pre-`min`/`max` & `std::sort` guards, real `sample_test.mp4` + `file:///` reference analysis via `ShotDetector(ClassicalOnly)` / visual / audio perception + `clearReference()`, `PromptParser` + `DeterministicPlanner` + hardened `ProposalBridge::validate()`/`dry_run()` + Spec `3.3 #3` `proposedOperations` formatting, atomic `applyProposal()` `CompositeCommand` 1-step undo/redo + stale conflict + partial-failure reverse rollback + `PlaybackSnapshotStore` republish, `AVIOInterruptCB` cooperative abort, safe mid-worker `delete` (`QPointer` + `alive_flag_`), and concurrent GUI edit isolation (6 GoogleTest cases)
30. `Phase10QmlOffscreenSmoke` — Native `ave_desktop.exe --verify-qml` under `QT_QPA_PLATFORM=offscreen` + `QT_QUICK_BACKEND=software` loading `qrc:/ave/qml/main.qml` with all 5 C++/Qt services and `LiveFrameImageProvider("aveframe")` registered, failing on any QML error/warning or creation failure and verifying clean reverse shutdown

---

## Current Milestone Standing (2026-11-12 — M1.1A through M1.4A & M2.1A Complete)
- Day 0 local prerequisites (`Qt 6.8.3 msvc2022_64`, `vcpkg` `gtest` + `nlohmann-json`, `FFmpeg n8.1.3 LGPLv3`), **Phase 10 Tasks 10.1–10.9 (G-01..G-09)**, **M1.1–M1.4A**, and **M2.1A** are complete:
  - **M1.1A (`ecef80d`) / M1.1B (`505585c`)**: Split metadata preservation, cross-track type validation, and `Track::locked()` enforcement across all `CommandBus` mutations.
  - **M1.2A (`d7afd60`) / M1.2B (`cd3765b`)**: Structured `nlohmann::json` versioned (`1.2.0`) serialization for `Timeline`, `Project`, `MediaAsset`, `MediaPool`, `MediaBin`, and `MediaClip`.
  - **M1.3A (`c0087c1`) / M1.3B (`c559899`)**: Unified dirty-state tracking across `CommandHistory`, `MediaPool`, and `Project`, plus Save / Don't Save / Cancel lifecycle dialogs.
  - **M1.3C1 (`714d122`) / M1.3C2 (`6cd3717`)**: Atomic SHA-256 autosave snapshots (`ProjectAutosaveEngine`, `RecoveryInspector`, `AutosaveService`) and startup/`File -> Open` recovery discovery, inspection, restore, discard, and lifecycle cleanup (`RecoveryDialog.qml`).
  - **M1.4A (`411831e`)**: Persistent, versioned (`v1`), SHA-256-verified source-addressed `ave::MediaCache` (`<CacheLocation>/media` in `ave_desktop.exe`), bounded disk & memory LRU eviction with active reader/writer pinning, sequence-fenced atomic replacement, single-flight extraction deduplication, `MediaImportQueue` decode avoidance on cache hit, non-blocking async thumbnail & waveform hydration on `MediaPoolModel` and `TimelineModel` project reopen/relink, and graceful fallback on missing/corrupt/unwritable storage.
  - **M2.1A (ADR-0020)**: Windows native shared-mode WASAPI audio output (`ave::WasapiAudioDevice` + `ave::IWasapiEndpoint`, dedicated COM MTA worker thread isolated from Qt STA GUI thread, `AudioFormatConverter` format/channel/rate adaptation, `AUDCLNT_E_DEVICE_INVALIDATED` handling), non-silent `PlaybackSession` backend binding (`AudioBackendMode::AutoPlatformDefault` vs explicit `NullForTesting`), seek/stop stale-audio buffer flush, `PlaybackService` reactive audio diagnostics (`21` properties), and minimal status/error cues in `AudioMeterView.qml` and `ToolbarView.qml`. Known decoder/mixer limitations (`StreamAudioDecoder` & multi-clip mixing/slicing) remain separate subsequent milestones.
- Next milestone: **M2.1B+** / subsequent stages per `docs/PROFESSIONAL_NLE_ENGINEERING_PLAN.md` (alongside pending Task 10.10 manual GUI & physical speaker playback verification).
