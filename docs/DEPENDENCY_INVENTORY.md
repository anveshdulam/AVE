# Third-Party Dependency and License Inventory

This document tracks all external libraries, SDKs, and dependencies for the Autonomous Video Editor, documenting their version, purpose, license, and distribution obligations.

---

## 1. Phase 0 Dependencies (Initial Baseline)

| Dependency | Version / Baseline | Source | License | Purpose | Distribution Notes |
|---|---|---|---|---|---|
| **GoogleTest** | 1.14.0 (via vcpkg) | github.com/google/googletest | BSD-3-Clause | Unit and integration test framework | Test-only dependency; not distributed in release binaries |
| **nlohmann-json** | 3.11.3 (via vcpkg) | github.com/nlohmann/json | MIT | JSON schema validation and IPC DTO serialization | Permissive header-only library; include notice in release |
| **LunarG Vulkan SDK** | 1.3 / 1.4 | vulkan.lunarg.com | Apache-2.0 | Vulkan headers, validation layers, shader tools | Dynamic link against OS loader (`vulkan-1.dll`) |

---

## 2. Planned Subsystem Dependencies (Phased Rollout)

| Subsystem / Dependency | Planned Version | License | Target Layer | Compliance / IP Constraints |
|---|---|---|---|---|
| **Qt 6 (Core, Gui, Quick, Qml)** | 6.7+ | LGPLv3 / Commercial | Layer 1 (Presentation) | Comply with LGPLv3 dynamic linking & user relinking requirements |
| **FFmpeg (libavformat, libavcodec, libswresample)** | 6.x / 7.x | LGPLv2.1+ | Layer 4 (Media & Data) | Compile with `--disable-gpl --disable-nonfree` for LGPL compliance |
| **OpenColorIO (OCIO)** | 2.4+ / 2.5+ | BSD-3-Clause | Layer 3 (Real-Time Color) | ACES color configurations; permissive license |
| **OpenTimelineIO (OTIO)** | 0.16+ | Apache-2.0 | Layer 4 (Interchange) | Non-authoritative timeline import/export interchange |
| **SQLite** | 3.45+ | Public Domain | Layer 4 (Project Database) | Local project database in WAL mode |
| **PortAudio** | 19.7+ | MIT-like | Layer 3 (Audio Device I/O) | Device I/O abstraction; permissive license |
| **PyTorch / ONNX Runtime** | Latest stable | BSD-3-Clause / MIT | Layer 2 (AI Worker) | Local inference runtime; no cloud API dependencies |

---

## 3. Licensing Audit Rules
1. Never enable GPL-only or non-commercial flags without an explicit ADR and licensing sign-off.
2. Maintain clear separation between LGPL shared libraries and proprietary core code.
3. Include third-party license notices in installer and About dialog.

