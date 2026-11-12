# Third-Party Dependency and License Inventory

This document tracks external C++ SDKs, runtime libraries, and separate Python tooling used by the Autonomous Video Editor, verifying versions, licenses, and integration status.

> [!NOTE]
> **CI & Local LGPL Compliance Status:** [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) now provisions `ffmpeg-master-latest-win64-lgpl-shared.zip` (`FFMPEG_DIR`), installs **Qt 6.8.3 (`win64_msvc2022_64`)**, resolves `vcpkg` manifest dependencies, and verifies `ffmpeg -buildconf` + `dumpbin /dependents` contain zero `--enable-gpl` / `--enable-nonfree` flags or GPL runtime DLLs matching [ADR-0005](adr/0005-ffmpeg-media-foundation.md). Before shipping release binaries, bundle LGPL notices and user-relinking instructions.

---

## 1. Native C++ Build & Runtime Dependencies

| Dependency | Pinned / Audited Version | Source / Location | License | Status in `build/smoke` (`2026-11-10`) | Purpose & Compliance Notes |
|---|---|---|---|---|---|
| **Qt 6 (`Core`, `Gui`, `Quick`, `Qml`, `QuickControls2`, `Test`)** | **6.8.3 (`msvc2022_64`)** (Selected **open-source LGPLv3 compatibility baseline**) | Qt Online Installer / `aqtinstall` | **LGPLv3 / Commercial** | **NOT INSTALLED (`Qt6_DIR-NOTFOUND`)** | Mandatory **local build prerequisite** for Phase 10 native desktop UI. Qt 6.8.3 is an open-source 6.8 patch release; ongoing commercial-only 6.8.x LTS patch drops are not assumed. Preserve dynamic linking + LGPLv3 notices. |
| **FFmpeg (`libavformat`, `libavcodec`, `libavutil`, `libswscale`, `libswresample`)** | `n8.1.3-34-ga35157abb9-20261106` (`D:\DevTools\ffmpeg`) | `D:\DevTools\ffmpeg` (local) & CI `win64-lgpl-shared` (`.github/workflows/ci.yml`) | **LGPLv3** locally & in CI (`--enable-version3 --enable-shared`; zero `--enable-gpl` / `--enable-nonfree`) | Active (`build/x64-debug`, `build/x64-release`) | MSVC `.lib` import libraries (`av*.lib`, `sw*.lib`) verified present; `dumpbin /dependents` confirms DLLs depend solely on Windows UCRT, OS DLLs, and peer FFmpeg DLLs (**zero** external MinGW/GPL codec DLLs). `.github/workflows/ci.yml` verifies `-buildconf`, `FFmpeg_INCLUDE_DIR`, and `dumpbin` linkage on every run; ship LGPL notices & relinking instructions with releases. |
| **LunarG Vulkan SDK** | `1.4.363.0` | `C:\VulkanSDK\1.4.363.0` | **Apache-2.0** | Active (`VulkanRenderTest` passed on Intel Arc `1.4.325`) | Vulkan headers, validation layers (`VK_LAYER_KHRONOS_validation`), GLSL compiler (`glslc`). |
| **GoogleTest** | `1.14.0+` (via `vcpkg.json`) | `vcpkg` (`github.com/google/googletest`) | **BSD-3-Clause** | **NOT INSTALLED (`GTest_DIR-NOTFOUND`; `vcpkg_installed/` holds metadata only)** | Required **local build prerequisite** (`vcpkg` manifest install) for standardized Phase 10 unit/integration suites (`GTest::gtest` with explicit Qt `main()` where Qt loops are needed, or `GTest::gtest_main` for pure C++ suites). |
| **nlohmann-json** | `3.11.3+` (via `vcpkg.json`) | `vcpkg` (`github.com/nlohmann/json`) | **MIT** | **NOT INSTALLED (`nlohmann_json_DIR-NOTFOUND`)** | Resolved via `vcpkg` manifest preset (`CMakePresets.json`); currently `core/` JSON codecs use custom string routines where optional. |

---

## 2. External Python Environment (Non-C++-Production Tooling)

These packages exist in the host `Python 3.11.9` installation and are **NOT linked, loaded, imported, or executed by `ave_desktop.exe`**:

| Package | Audited Host Version | License | Role & Phase Boundary |
|---|---|---|---|
| **PySide6** | `6.12.0` | LGPLv3 / GPL | Temporary mockup runner (`apps/desktop/launch_gui.py` / `run_editor.bat`); retired when Phase 10 native C++/Qt app launches |
| **PyTorch (`torch`)** | `2.3.1` | BSD-3-Clause | External Phase 8 offline model research/training only; no training scripts or `.pt` weights exist in repo |
| **ONNX Runtime (`onnxruntime`)** | `1.26.0` | MIT | External Python environment only; C++ `ave_ai` currently uses `IModelRunner` mock/CPU stubs without linking `onnxruntime.dll` |
| **NumPy (`numpy`)** | `1.26.4` | BSD-3-Clause | Python data/training support package only |

---

## 3. Planned / Unassigned Future C++ Dependencies

| Dependency | Planned Version | License | Target Layer | Status |
|---|---|---|---|---|
| **OpenColorIO (OCIO)** | `2.4+` | BSD-3-Clause | Layer 3 (Color) | Deferred beyond Phase 10 |
| **OpenImageIO (OIIO)** | `2.5+` | Apache-2.0 | Layer 4 (Media) | Deferred beyond Phase 10 |
| **PortAudio / WASAPI** | `19.7+` / Win32 | MIT-like / OS | Layer 3 (Audio I/O) | Deferred (`NullAudioDevice` active in Phase 10) |
| **SQLite** | `3.45+` | Public Domain | Layer 4 (Media DB) | Deferred (projects currently save atomic `.aveproj` JSON) |

---

## 4. Licensing Audit Rules
1. Never ship GPL FFmpeg binaries without explicit ADR approval; preserve LGPL shared builds (`win64-lgpl-shared`, no `--enable-gpl` or `--enable-nonfree`) verified in `.github/workflows/ci.yml` matching ADR-0005.
2. Preserve dynamic linking boundaries for Qt 6 (LGPLv3) and FFmpeg (LGPLv3).
3. Bundle complete third-party license texts and LGPL user-relinking information in any distributed build.
