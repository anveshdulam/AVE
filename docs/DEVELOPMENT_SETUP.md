# Development Environment Setup Guide

## Host Target
- **Primary OS:** Windows 11 (64-bit)
- **Target Architecture:** x64
- **Host Hardware Profile:** Intel Core Ultra 5 125H (14 cores, 18 threads), Intel Arc Graphics (Vulkan 1.4.325), 16 GB LPDDR5X RAM

---

## 1. Local Build Prerequisites

### A. Visual Studio 2022 (or Build Tools)
1. Run the Visual Studio Installer (`vs_installer.exe`).
2. Select **"Desktop development with C++"**.
3. Ensure the following optional components are checked:
   - MSVC v143 - VS 2022 C++ x64/x86 build tools (`19.44+`)
   - Windows 11 SDK (`10.0.22621.0` or newer)
   - C++ CMake tools for Windows
   - C++ Clang Compiler for Windows (optional, recommended for static analysis)

### B. CMake & Ninja
If not installed via Visual Studio:
- Install CMake `3.25+` and Ninja `1.13+`.
- Ensure `cmake` and `ninja` are accessible in your system `PATH`.

### C. LunarG Vulkan SDK
1. Download the Windows Vulkan SDK from [vulkan.lunarg.com](https://vulkan.lunarg.com/).
2. Install to `C:\VulkanSDK\<version>` (audited host: `1.4.363.0`).
3. Verify `VULKAN_SDK` is set and `VK_LAYER_KHRONOS_validation` is registered.

### D. FFmpeg Shared LGPLv3 SDK (`D:\DevTools\ffmpeg`)
- `cmake/FindFFmpeg.cmake` resolves headers from `D:\DevTools\ffmpeg\include`, MSVC import `.lib` files (`avcodec.lib`, `avformat.lib`, `avutil.lib`, `swresample.lib`, `swscale.lib`) from `D:\DevTools\ffmpeg\lib`, and shared DLLs (`avcodec-62.dll`, `avformat-62.dll`, `avutil-60.dll`, `swresample-6.dll`, `swscale-9.dll`) from `D:\DevTools\ffmpeg\bin`.
- Verified via `dumpbin /dependents`: these DLLs depend only on Windows Universal CRT (`api-ms-win-crt-*.dll`), standard Windows OS DLLs, and peer FFmpeg DLLs (**zero external MinGW runtime DLL dependencies**).
- Must be an **LGPL** build (no `--enable-gpl`) matching [ADR-0005](adr/0005-ffmpeg-media-foundation.md).

### E. `vcpkg` (Package Manager)
To preserve space on drive `C:\`, bootstrap `vcpkg` at `D:\vcpkg`:
```powershell
git clone https://github.com/microsoft/vcpkg.git D:\vcpkg
D:\vcpkg\bootstrap-vcpkg.bat
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", "D:\vcpkg", "User")
```
Using `CMakePresets.json` (`x64-debug` / `x64-release`) installs manifest dependencies from `vcpkg.json` (`gtest`, `nlohmann-json`).

### F. Qt 6 (`6.8.3` Open-Source LGPLv3 Compatibility Baseline)
1. Install **Qt 6.8.3 (`msvc2022_64`)** (via Qt Online Installer or `aqtinstall` into `D:\Qt\6.8.3\msvc2022_64`). *Note: Qt 6 is currently not installed on the audited host (`Qt6_DIR-NOTFOUND`) and is a mandatory local build prerequisite before Phase 10 native desktop builds can resolve `Qt6` packages. Qt 6.8.3 is pinned as our open-source LGPLv3 compatibility baseline (ongoing commercial-only 6.8.x LTS maintenance patches are not assumed).*
2. Ensure required components/modules are present: `Core`, `Gui`, `Quick`, `Qml`, `QuickControls2`, `Test` (plus QML module `QtQuick.Dialogs`).
3. Add `D:\Qt\6.8.3\msvc2022_64\bin` to `PATH` and configure with `-DCMAKE_PREFIX_PATH="D:/Qt/6.8.3/msvc2022_64"` (or set `Qt6_DIR`).

---

## 2. CI Workflow Configuration
- [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) provisions **Qt 6.8.3 (`win64_msvc2022_64`)** via `jurplel/install-qt-action@v4`, **LGPL shared** FFmpeg (`ffmpeg-master-latest-win64-lgpl-shared.zip`, matching [ADR-0005](adr/0005-ffmpeg-media-foundation.md)), and **`vcpkg`** (`x64-windows` binary-cached manifest dependencies `gtest` and `nlohmann-json`).

---

## 3. Configuring, Building, and Testing

Configure with the preset so `vcpkg` installs `gtest` and `nlohmann-json`:
```powershell
cmake --preset x64-debug -DCMAKE_PREFIX_PATH="D:/Qt/6.8.3/msvc2022_64"
```

*(Configuring outside presets without `-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake`, as in the current `build/smoke` tree, leaves `GTest_DIR-NOTFOUND` and `nlohmann_json_DIR-NOTFOUND`.)*

### Build & Run Tests (Debug)
```powershell
cmake --build --preset x64-debug
ctest --preset x64-debug --output-on-failure
```

### Run Vulkan Capability Probe
```powershell
.\build\x64-debug\engines\vulkan_probe\ave_vulkan_probe.exe
```

---

## 4. Storage and Memory Guidelines
- Keep `C:\` free space monitored (maintain > 20 GB free).
- Place heavy toolchains, `vcpkg` caches, builds, and media test fixtures on `D:\`.
