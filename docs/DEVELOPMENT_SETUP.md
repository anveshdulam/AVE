# Development Environment Setup Guide

## Host Target
- **Primary OS:** Windows 11 (64-bit)
- **Target Architecture:** x64
- **Host Hardware Profile:** Intel Core Ultra 5 125H (14 cores, 18 threads), Intel Arc Graphics (Vulkan 1.4.325), 16 GB LPDDR5X RAM

---

## 1. Prerequisites Installation

### A. Visual Studio 2022 (or Build Tools)
1. Run the Visual Studio Installer (`vs_installer.exe`).
2. Select **"Desktop development with C++"**.
3. Ensure the following optional components are checked:
   - MSVC v143 - VS 2022 C++ x64/x86 build tools (latest)
   - Windows 11 SDK (10.0.22621.0 or newer)
   - C++ CMake tools for Windows
   - C++ Clang Compiler for Windows (optional, recommended for static analysis)

### B. CMake & Ninja
If not installed via Visual Studio:
- Install CMake 3.25+ and Ninja.
- Ensure `cmake` and `ninja` are accessible in your system `PATH`.

### C. LunarG Vulkan SDK
1. Download the Windows Vulkan SDK from [vulkan.lunarg.com](https://vulkan.lunarg.com/).
2. Run installer to install to `C:\VulkanSDK\<version>`.
3. Verify that environment variable `VULKAN_SDK` is automatically set and that `VK_LAYER_KHRONOS_validation` is registered.

### D. vcpkg (Package Manager)
To preserve space on drive `C:\`, install vcpkg on `D:\vcpkg`:
```powershell
git clone https://github.com/microsoft/vcpkg.git D:\vcpkg
D:\vcpkg\bootstrap-vcpkg.bat
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", "D:\vcpkg", "User")
```

### E. Qt 6 (Presentation Framework)
1. Install Qt 6.7+ for MSVC 2022 64-bit (via Qt Online Installer or `aqtinstall`) to `D:\Qt`.
2. Add `D:\Qt\<version>\msvc2022_64\bin` to `PATH` or set `CMAKE_PREFIX_PATH`.

---

## 2. Configuring, Building, and Testing

The repository uses `CMakePresets.json` with vcpkg manifest mode.

### Configure (Debug)
```powershell
cmake --preset x64-debug
```

### Build (Debug)
```powershell
cmake --build --preset x64-debug
```

### Run Tests (CTest)
```powershell
ctest --preset x64-debug
```

### Run Vulkan Capability Probe
```powershell
.\build\x64-debug\engines\vulkan_probe\ave_vulkan_probe.exe
```

---

## 3. Storage and Memory Guidelines
- Keep `C:\` free space monitored (maintain > 20 GB free).
- All heavy package builds, caches, and test fixtures are located on `D:\`.

