---
name: vulkan-rendering
description: Designs and implements the Vulkan rendering hardware interface, GPU resource lifetime, command submission, synchronization, render passes, shader pipeline and Windows-first GPU validation.
---

# Vulkan Rendering

## Scope
Own the Vulkan backend and its platform boundary. The render graph should depend on a backend-neutral RHI; Vulkan is the initial implementation, not a reason to spread Vulkan types throughout the application.

## Required concepts
- Instance/device/queue selection and capability reporting.
- Explicit memory/resource ownership and lifetime.
- Command pools/buffers, descriptor management and pipeline cache.
- Image layouts, barriers, queue ownership and synchronization.
- Frames-in-flight and swapchain recreation.
- Device-loss handling and deterministic shutdown.
- Shader compilation/versioning and validation layers in development.
- GPU timestamps and debug labels.

## Rules
1. Enable validation layers in development builds.
2. Never assume a particular GPU, queue family, format or extension exists.
3. Check capabilities and select supported paths.
4. Do not block the UI thread waiting for GPU completion.
5. Avoid CPU↔GPU round trips in the steady-state render path.
6. Define resource lifetime and synchronization before implementing effects.
7. Handle window resize, DPI changes, device recreation and out-of-date swapchains.
8. Keep Vulkan handles out of QML and public application-core APIs.
9. Add a minimal triangle and offscreen texture test before building a complex graph.
10. Profile before introducing advanced synchronization or memory allocators.

## Qt interop gate
Before using Vulkan-backed frames in Qt Quick, create a small interop spike for the exact pinned Qt version and Windows driver stack. Document ownership, synchronization and fallback behavior. Do not assume QQuickRhiItem or Qt's rendering hardware interface guarantees zero-copy interop without proof.

## Acceptance
Validation-layer-clean rendering, correct resize/recreation, measurable frame timing and no unbounded GPU resource growth.
