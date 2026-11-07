---
name: gpu-render-graph
description: Designs the backend-neutral render graph for compositing, transforms, effects, transitions, masks, keyframes, caching and final export, using Vulkan as the first backend.
---

# GPU Render Graph and Compositing

## Architecture
Build a directed acyclic graph (DAG) of typed image/audio operations with explicit dependencies, formats, color spaces, dimensions, time domains and resource lifetimes. Timeline evaluation produces a graph for a specific frame/time.

## Initial node set
Source decode, color conversion, transform, opacity, blend/composite, crop, resize, blur, basic color adjustment, transition, text overlay and output. Add advanced nodes only after graph scheduling and caching are correct.

## Rules
- Separate graph description from backend execution.
- Define node input/output contracts and color-space semantics.
- Detect cycles and invalid connections.
- Cache only when inputs, parameters, time and color context match.
- Keep intermediate frames GPU-resident when practical.
- Define CPU fallback for nodes not yet implemented on GPU.
- Make effect parameters keyframeable through a common parameter model.
- Treat final export separately from interactive preview quality.
- Do not claim bit-exact parity between GPU and CPU paths without tests.

## Acceptance
Graph validation tests, deterministic test patterns, effect reference images, cache invalidation tests, memory-pressure behavior and performance profiling.
