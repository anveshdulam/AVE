# Known Limitations and Hardware Constraints

**Date:** 2026-11-06  
**Milestone:** Phase 0 — Foundation  

This document tracks verified limitations of the current development environment and baseline engine.

---

## 1. Hardware Constraints

### A. Integrated GPU (Intel Arc Xe-LPG)
- **No NVIDIA GPU:** The development machine has no discrete NVIDIA GPU. Proprietary NVIDIA CUDA, NVENC, and NVDEC APIs are **not supported** on this hardware.
- **Shared Memory:** Dedicated adapter RAM is reported as 2 GB, sharing up to ~8 GB of system memory dynamically. High-bandwidth 4K/8K uncompressed frame manipulation in VRAM must be managed with strict texture pools and memory budgeting.
- **Hardware Acceleration Verification:** While Intel Quick Sync Video (QSV) and Vulkan Video are architectural possibilities, neither is claimed as active until verified with an isolated proof of concept.

### B. System RAM Ceiling (16 GB)
- The host has 16 GB soldered LPDDR5X RAM with ~2.7 GB free under baseline OS operation.
- Heavy workloads (e.g., training large neural networks or holding multiple uncompressed 4K video frame caches in RAM) are infeasible.
- AI model training is strictly limited to small, specialized Transformers (5M–30M parameters) operating on compact precomputed feature sequences.

---

## 2. Phase 0 Engineering Scope Constraints

1. **No Timeline Editing:** The full multi-track timeline UI, clip trimming tools, and canvas interactions belong to Phase 3.
2. **No Custom Codec Decoders:** Media ingest and packet demuxing belong to Phase 1.
3. **No Complex Render Graph:** Multi-pass compositing, masks, and OpenFX hosting belong to Phase 4 and Phase 8.
4. **No Remote AI APIs:** Production builds will never depend on paid third-party cloud inference or telemetry.
5. **Compiler Prerequisite Block:** Build execution and test running are currently pending installation of Visual Studio 2022 C++ tools on the host.

