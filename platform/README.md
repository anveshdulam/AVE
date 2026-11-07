# Layer 5 — Platform

## Responsibilities
- Operating system windowing, process management, and file system primitives (Windows 11 x64 baseline).
- GPU hardware drivers and Vulkan loader boundary (`vulkan-1.dll`).
- Hardware codec interfaces (Intel Quick Sync Video, Vulkan Video).
- Audio device interfaces (PortAudio / WASAPI).
- High-resolution timers and thread priority scheduling.

## Boundary Rules
- Platform-specific code remains strictly isolated behind backend-neutral abstractions.
- Cross-platform architecture is maintained for future Linux and macOS targets.

