---
name: release-packaging-licensing
description: Plans Windows-first packaging, installer layout, runtime dependencies, FFmpeg and Qt license compliance, third-party notices, versioning and release validation.
---

# Release, Packaging and Licensing

## Rules
- Maintain a third-party dependency inventory with version, source, license, modifications and distribution obligations.
- Review Qt licensing for the selected modules and distribution model.
- Review FFmpeg build configuration and LGPL/GPL implications before redistributing binaries.
- Do not assume codec patent rights are granted by an open-source license.
- Do not bundle proprietary camera RAW SDKs without explicit legal approval.
- Record Vulkan loader/runtime expectations and driver prerequisites.
- Package only tested binaries and required runtime assets.
- Keep model weights separately licensed and attributed.
- Generate SBOM and checksums for release artifacts where practical.

## Release gates
Clean-machine installation, launch, media import, playback, export, uninstall/upgrade, license notices and offline operation must be tested.
