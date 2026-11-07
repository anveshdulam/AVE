---
name: timeline-ui-interaction
description: Implements professional timeline interaction in Qt/QML, including track layout, clip selection, trim tools, snapping, zoom, scrolling, playhead, markers, drag-and-drop and accessibility.
---

# Timeline UI Interaction

## Boundary
The timeline UI renders a view of the project model and emits typed commands. It must not own authoritative clip timing or mutate project state directly.

## Interaction model
- Define tool modes and keyboard shortcuts.
- Support selection, move, trim, ripple trim, split, delete, snapping, zoom and scroll.
- Use stable entity IDs and optimistic visual previews where appropriate.
- Distinguish preview gesture state from committed project state.
- Commit a gesture as one undoable command/transaction.
- Use rational time conversion for pixel-to-time mapping.
- Virtualize tracks and clip visuals for large projects.
- Ensure high-DPI scaling and accessible focus/keyboard operation.

## Acceptance
Interaction tests for snapping, edge cases, multi-selection, undo grouping, large timelines, mixed frame rates and project updates from non-UI sources.
