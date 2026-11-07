---
name: performance-profiling
description: Profiles CPU, GPU, memory, disk I/O, decode, playback, timeline evaluation, cache behavior and rendering performance, then recommends evidence-based optimizations.
---

# Performance and Profiling

## Method
1. Define the user-visible symptom and target workload.
2. Record hardware, driver, OS, build mode, media properties and project settings.
3. Capture baseline metrics.
4. Profile the actual bottleneck using suitable CPU/GPU tools.
5. Change one major variable at a time.
6. Re-run identical benchmarks and compare.
7. Add regression thresholds only after stable baselines exist.

## Metrics
Startup, import/probe latency, thumbnail generation, seek latency, frame decode time, render-graph CPU time, GPU execution time, dropped frames, A/V drift, cache hit rate, peak RAM/VRAM, export throughput and thermal throttling.

## Rules
- Do not optimize based on intuition alone.
- Avoid synchronous disk I/O on playback/render critical paths.
- Bound caches and queues.
- Account for 16 GB system RAM and the actual GPU VRAM of the developer machine.
- Provide lower-resolution proxy and preview modes.
- Preserve correctness while optimizing.
