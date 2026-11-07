# ADR-0007: Backend-Neutral GPU Render Graph and Compositing Pipeline

## Status
Accepted

## Date
2026-11-07

## Context
A professional non-linear video editor requires a multi-track compositing engine to combine overlapping video clips, graphic overlays, title cards, and transitions. Each frame on the timeline may require source decoding, spatial transformations (pan, tilt, zoom, rotation), opacity adjustment, color space mapping, and alpha blending across multiple layers.

Key architectural and performance requirements:
1. **Backend Neutrality:** The graph description and scheduling must remain completely decoupled from underlying GPU hardware APIs (Vulkan, Direct3D 12, Metal, CPU fallback). Vulkan handles, command buffers, and pipelines must not leak into the timeline model or application core.
2. **Deterministic Topological Execution:** The render graph must be represented as a Directed Acyclic Graph (DAG) with explicit dependency ordering and cycle detection.
3. **Resource Re-use & Bounded Memory:** Intermediate render targets (framebuffers/textures) must be pooled and recycled across passes to prevent unbounded GPU VRAM allocation.
4. **CPU Reference Fallback:** Every node must support a deterministic CPU reference evaluation path for automated testing, CI environments without physical GPUs, and offline background rendering.
5. **Timeline Snapshot Integration:** For any given playhead timestamp, timeline track evaluation generates a clean render graph representing all visible layers ordered from bottom track (background) to top track (foreground).

## Decision
1. **Subsystem Placement:** The render graph engine resides in Layer 3 (`engines/render`), producing the `ave_render` target.
2. **Node Architecture:**
   - Base class: `ave::RenderNode` with typed input and output image ports (`PortId`, `RenderImageDescriptor`).
   - Initial node set:
     - `SourceNode`: Accepts raw decoded video frames or solid color backgrounds.
     - `TransformNode`: Computes 2D affine transformations (matrix $3 \times 3$, translation, scale, rotation).
     - `OpacityNode`: Multiplies alpha channel by scalar $[0.0, 1.0]$.
     - `BlendNode`: Blends foreground over background supporting Porter-Duff Over, Add, Multiply, and Screen blend modes.
     - `OutputNode`: Final composite collector matching the sequence resolution and color space.
3. **Graph Validation & Cycle Detection:**
   - Graph compilation performs Kahn's algorithm or DFS topological sort to determine node evaluation order.
   - Throws descriptive errors or returns validation status if cycles or unconnected required inputs are detected.
4. **Intermediate Target Pooling:**
   - `ave::RenderResourcePool` caches textures by resolution and pixel format (`RGBA8_UNORM`, `RGBA16_SFLOAT`), recycling targets as soon as their consumers finish execution.
5. **Compositing Math:**
   - Standard premultiplied alpha Porter-Duff compositing equations:
     - $\text{Over}: C_{out} = C_{fg} + C_{bg} \cdot (1 - \alpha_{fg})$, $\alpha_{out} = \alpha_{fg} + \alpha_{bg} \cdot (1 - \alpha_{fg})$.
     - $\text{Add}: C_{out} = \min(1, C_{fg} + C_{bg})$.
     - $\text{Multiply}: C_{out} = C_{fg} \cdot C_{bg}$.
     - $\text{Screen}: C_{out} = 1 - (1 - C_{fg}) \cdot (1 - C_{bg})$.

## Alternatives Considered
- **Direct Vulkan Pipeline Hardcoding:** Tightly coupling timeline clips directly to Vulkan render passes. Rejected due to extreme fragility, inability to test in headless CI, and lack of cross-platform portability.
- **Immediate-Mode OpenGL Style Drawing:** Renders layers sequentially directly to the backbuffer without an explicit graph. Rejected because it precludes shader pass merging, render target recycling, and asynchronous GPU scheduling.

## Consequences
- **Positive:** Modular, testable compositing pipeline with zero hardware dependency in the graph layer; intermediate buffer reuse; deterministic CI testing.
- **Negative:** Requires node abstraction boilerplate and translation between graph operations and concrete GPU draw commands.

