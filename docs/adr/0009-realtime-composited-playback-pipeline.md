# ADR-0009: Real-Time Composited Playback Pipeline and Presentation Integration

## Status
Accepted

## Date
2026-11-08

## Context
In Phase 2, `PlaybackSession` and `PlaybackClock` were established for single-stream audio-video synchronization and frame prefetching. In Phase 3 Steps 1 and 2 (ADR-0007, ADR-0008), the backend-neutral Render Graph and Vulkan offscreen compute compositor were implemented.

However, to achieve a true non-linear editor (NLE) playback experience (Phase 3 Step 3), the presentation shell and playback engine must deliver real-time multi-track composited frames:
1. **Multi-Track Evaluation:** Multiple overlapping video tracks (e.g., background plate on V1, picture-in-picture overlay on V2, titles on V3) must be dynamically queried at the current playhead timestamp (PTS).
2. **Spatial Transformations & Blending:** Each active clip can possess spatial transformations (translation $X/Y$, scaling $X/Y$, rotation in degrees, anchor point), opacity ($[0.0, 1.0]$), and blend modes (`Over`, `Multiply`, `Screen`, `Add`), serialized non-destructively in `TimelineItem`.
3. **Hardware & Reference Consistency:** Frames decoded from media assets or generated from color solids must route through the `RenderGraph` DAG, utilizing `VulkanCompositor` when an accelerated GPU is present (such as the Intel Arc GPU) or falling back transparently to the deterministic CPU reference path in headless/CI test environments.
4. **Architectural Separation:** The presentation layer (`apps/desktop`) must consume finished presentation frames via ViewModels without direct access to decoding threads, compute shader dispatch, or raw Vulkan resources.

## Decision
1. **TimelineItem Visual Properties:**
   - Extended `TimelineItem` with 2D transform, opacity, and blend mode state:
     - `transform_x`, `transform_y` (pixel offsets relative to canvas center/anchor)
     - `scale_x`, `scale_y` (scaling multipliers, default $1.0$)
     - `rotation` (rotation angle in degrees)
     - `anchor_x`, `anchor_y` (normalized origin coordinates $[0.0, 1.0]$, default $(0.5, 0.5)$)
     - `opacity` (alpha multiplier $[0.0, 1.0]$, default $1.0$)
     - `blend_mode` (string descriptor, default `"Over"`)
   - Implemented JSON serialization and deserialization in `core/src/timeline.cpp` with round-trip unit test verification.

2. **Dynamic Render Graph Synthesis (`RenderGraph::build_for_timeline`):**
   - Implemented timeline-to-graph synthesis in `engines/render/src/render_graph.cpp`.
   - Iterates over unmuted video tracks from bottom to top (V1, V2, ...).
   - Queries `track.item_at(pts)` to find active items.
   - For each active clip, injects:
     - `SourceNode` referencing the track's decoded frame or solid color.
     - `TransformNode` applying affine rotation, scaling, and translation about the specified anchor point.
     - `OpacityNode` applying the item's alpha multiplier.
     - `BlendNode` compositing the layer over the accumulated canvas using the requested blend mode.
   - An `OutputNode` anchors the final composited image.

3. **PlaybackSession Real-Time Compositing Pipeline:**
   - Added `enable_compositing` boolean flag to `PlaybackSessionConfig`.
   - Added `set_vulkan_device(std::shared_ptr<VulkanDevice>)` allowing the presentation shell or runtime bootstrap to attach hardware GPU compute capabilities.
   - In `PlaybackSession::update()`:
     - Gathers active video track items and counts active visual layers.
     - Builds dynamic `RenderGraph` at current playhead PTS.
     - Executes compositing pass via `VulkanCompositor` (hardware path) or CPU rasterization (reference path).
     - Wraps composited `RenderImage` into a `VideoFrame` stamped with presentation time, duration, and seek generation token.
     - Sets `PresentationResult::is_composited = true` and reports `active_tracks_count`.

4. **Desktop Presentation ViewModels (`PlaybackViewModel`):**
   - Extended `PlaybackViewModel` with compositing status inspection (`is_compositing_active()`) and hardware accelerator description (`hardware_accelerator_name()`).
   - Integrated into desktop app smoke test (`apps/desktop/main.cpp`), verifying hardware discovery on Intel Arc Graphics, multi-track compositing pipeline execution, and SMPTE timecode accuracy.

5. **Automated Verification:**
   - Added `ave_compositing_integration_test` verifying multi-track Picture-in-Picture compositing (V1 background + V2 overlay with 50% scale, translation, 80% opacity, track muting, frame seeking, and timecode sync).

## Consequences
- **Positive:**
  - Complete vertical slice connecting timeline items, playback engine, render graph, Vulkan GPU compositor, and presentation shell.
  - Non-destructive spatial transformations and blend modes round-trip through project serialization.
  - Zero raw Vulkan exposure outside `engines/render`.
  - 100% test pass rate across all 14 automated test suites locally and in CI.
- **Negative:**
  - Multi-track timeline evaluation dynamically allocates RenderGraph nodes per frame; a retained-graph or node-caching optimization may be evaluated in future performance tuning.
