---
name: qt6-qml-presentation
description: Builds the native desktop presentation layer using Qt 6, QML and C++, including timeline UI, viewer shell, inspectors, media browser, accessibility and UI-to-core interfaces.
---

# Qt 6 / QML Presentation

## Responsibilities
Build presentation only. The UI displays application state and sends user intent through application services/commands. It does not decode media, own the canonical project model or run render work.

## Rules
- Use Qt Quick/QML for composable UI and C++ for performance-critical models and services.
- Keep expensive operations off the GUI thread.
- Expose narrow `QObject` facades or model interfaces; do not expose internal engine objects to QML.
- Use model/view patterns for media bins, tracks and clip lists.
- Keep timeline rendering virtualized; do not instantiate one QML object per frame.
- Separate UI state from project state.
- Use stable IDs, not display names, to address project entities.
- Provide keyboard shortcuts, focus behavior, scalable layouts and accessible labels.
- Never call FFmpeg or Vulkan directly from QML.

## Viewer integration
The viewer receives frame/surface handles from the playback/render subsystem. Validate the Qt Quick + Vulkan interop strategy with a minimal proof of concept before designing the full viewer. Check supported Qt APIs and platform behavior for the pinned Qt version.

## Acceptance
UI actions produce typed requests; state updates arrive through observable models/events; closing the UI cannot corrupt engine state; no media work blocks the GUI thread.
