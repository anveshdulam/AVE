# ADR-0001: Target Platform, Toolchain, and Build System

- **Status:** Accepted
- **Date:** 2026-11-06
- **Context:** The Autonomous Video Editor requires a high-performance native foundation capable of low-latency real-time video playback, GPU rendering, and local AI model integration. The initial development environment is a Windows 11 host (Intel Core Ultra 5 125H, Intel Arc Graphics). Cross-platform portability (Linux, macOS) is a core long-term architectural requirement.
- **Decision:**
  - Standardize on **C++20** as the main application and engine implementation language.
  - Standardize on **CMake 3.25+** as the canonical build system generator, utilizing `CMakePresets.json` for reproducible builds.
  - Standardize on **vcpkg in manifest mode (`vcpkg.json`)** with pinned baselines for third-party C++ dependencies.
  - Standardize on **MSVC x64 (Visual Studio 2022 / Build Tools v143)** as the primary Windows compiler.
- **Alternatives Considered:**
  - *Pure Rust stack:* High memory safety benefits, but C++20 provides direct, mature ABI integration with established professional media libraries (Qt 6, FFmpeg C APIs, Vulkan, OpenColorIO, OpenTimelineIO) without excessive FFI impedance mismatches.
  - *Custom build scripts / Meson:* Rejected in favor of CMake due to broad ecosystem support across vcpkg, Qt 6, and IDE tooling.
- **Consequences:**
  - Developers on Windows must have Visual Studio 2022 C++ tools and Windows 10/11 SDK installed.
  - Dependencies are declared declaratively and pinned, avoiding unrepeatable floating builds.
  - Rust remains an option for isolated services via an explicit boundary and separate ADR.
- **Risks:**
  - Disk space overhead of MSVC and vcpkg build trees on system drive `C:\`. Mitigated by directing vcpkg and build artifacts to volume `D:\`.
- **Validation Plan:**
  - Verify clean configuration and compilation using CMakePresets on a clean checkout once compiler tools are installed.

