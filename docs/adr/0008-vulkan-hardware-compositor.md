# ADR-0008: Vulkan Offscreen Hardware Device and Compute Compositing Pipeline

## Status
Accepted

## Date
2026-11-07

## Context
In Phase 3 Step 1 (ADR-0007), a backend-neutral compositing Render Graph (`RenderGraph`, `RenderNode`, `RenderImage`) was established with a deterministic CPU reference evaluation path. However, real-time multi-track video playback at 1080p, 4K, and high frame rates (60fps+) requires hardware GPU acceleration to offload pixel transformations, alpha blending, and color modulation.

The target system features an Intel Arc GPU (Xe-LPG GT2, Vulkan 1.4.325). The Vulkan rendering subsystem must adhere to the product constitution:
1. **Windows-first & Cross-Platform:** Use standard Vulkan 1.3+ specifications without proprietary vendor extensions.
2. **Backend-Neutral Isolation:** Keep raw Vulkan types (`VkInstance`, `VkDevice`, `VkCommandBuffer`) isolated inside Layer 3 (`engines/render`). Presentation (Qt) and Application Core must never include `<vulkan/vulkan.h>` directly.
3. **Graceful Fallback:** If a physical GPU device is missing, uninitialized, or in a headless CI environment, the Render Graph must seamlessly fall back to the deterministic CPU reference path without crashing.
4. **Deterministic Testing:** GPU compositing outputs must be validated against the CPU reference within 1 LSB (least significant bit) numerical tolerance.

## Decision
1. **Vulkan Device Management (`ave::VulkanDevice`):**
   - Encapsulates `VkInstance`, physical GPU enumeration and selection (preferring Discrete GPU > Integrated GPU), logical `VkDevice`, compute/graphics `VkQueue`, and `VkCommandPool`.
   - Supports optional validation layers (`VK_LAYER_KHRONOS_validation`) in debug builds.
   - Provides single-time command buffer helpers for staging buffer memory transfers and one-shot GPU pipeline dispatch.

2. **GPU Texture & Memory Lifecycle (`ave::VulkanTexture`):**
   - Encapsulates 2D `VkImage`, `VkDeviceMemory`, `VkImageView`, and `VkSampler`.
   - Manages memory barrier layout transitions (`VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL`, `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`, `VK_IMAGE_LAYOUT_GENERAL`).
   - Implements CPU $\leftrightarrow$ GPU pixel transfer:
     - `upload_from(const RenderImage&)`: Allocates host-visible staging buffer, copies CPU pixels, and issues `vkCmdCopyBufferToImage`.
     - `download_to(RenderImage&)`: Issues `vkCmdCopyImageToBuffer` into host-visible staging buffer and copies back to CPU.

3. **Compute Shader Pipeline (`ave::VulkanCompositor`):**
   - Implements GPU compositing via a Vulkan compute shader (`compositor.comp`) rather than a multi-pass rasterization swapchain.
   - Compute workgroup size: $(16, 16, 1)$ with thread-per-pixel mapping.
   - Push constants block ($96$ bytes) passes:
     - Output resolution $(W, H)$
     - Inverse affine transformation matrix $(m_{00}, m_{01}, m_{02}, m_{10}, m_{11}, m_{12})$
     - Anchor coordinates $(a_x, a_y)$
     - Opacity multiplier $[0.0, 1.0]$
     - Blend mode (`Over`, `Multiply`, `Screen`, `Add`)
     - Solid color toggle and RGBA values
   - Performs bilinear texture sampling, alpha multiplication, and Porter-Duff compositing directly into output storage images.
   - Ships with embedded precompiled SPIR-V byte headers (`compositor_comp_spv.hpp`) with CMake custom command compilation via `glslc` when available.

4. **Render Graph Execution Integration:**
   - When a `VulkanDevice` is provided to `RenderGraph::execute()`, hardware-accelerated nodes dispatch GPU compute passes; otherwise, nodes execute via the CPU reference path.

## Consequences
- **Positive:**
  - High-performance GPU hardware offscreen compositing on Intel Arc GPU.
  - Zero raw Vulkan exposure outside `engines/render`.
  - Full automated CTest coverage and deterministic CPU verification in headless environments.
- **Negative:**
  - Requires maintaining compute shader SPIR-V alongside CPU reference algorithms.

