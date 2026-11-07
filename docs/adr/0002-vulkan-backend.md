# ADR-0002: Vulkan as Initial GPU Rendering Backend

- **Status:** Accepted
- **Date:** 2026-11-06
- **Context:** Real-time video compositing, image processing, color transforms, and viewer presentation require high-performance, predictable GPU execution. The host environment features an Intel Arc GPU (Xe-LPG GT2) verified to support Vulkan API 1.4.325 via driver version 32.0.101.8132.
- **Decision:**
  - Implement an internal, backend-neutral **Render Hardware Interface (RHI)**.
  - Implement **Vulkan** as the first concrete GPU backend for Windows and Linux.
  - Keep Vulkan handles, command buffers, and descriptor sets strictly isolated from the Presentation (Qt) and Application Core layers.
  - Require Vulkan validation layers (`VK_LAYER_KHRONOS_validation`) in development builds.
- **Alternatives Considered:**
  - *Direct3D 12:* High quality on Windows, but platform-locked; Vulkan provides a single cross-vendor path for Windows and Linux.
  - *OpenGL:* Legacy API with unpredictable driver threading and lack of modern explicit synchronization.
  - *NVIDIA CUDA:* The physical machine contains an Intel Arc GPU with no discrete NVIDIA hardware. Proprietary CUDA compute is therefore unusable on this hardware.
- **Consequences:**
  - Rendering engine manages explicit resource lifetimes, synchronization fences, semaphores, and memory heaps.
  - A dedicated texture-sharing boundary will be implemented between the Vulkan render graph and the Qt viewer surface.
- **Risks:**
  - Vulkan synchronization bugs. Mitigated by continuous validation layer checks, strict resource ownership, and isolated capability spikes (`engines/vulkan_probe`).
- **Validation Plan:**
  - Execute `ave_vulkan_probe` to verify instance creation, physical device enumeration, queue capabilities, and validation layer activation without warnings.

