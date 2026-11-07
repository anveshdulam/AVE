---
name: architecture-and-adr
description: Designs and reviews the layered architecture, subsystem boundaries, interfaces, data ownership, dependency direction, and Architecture Decision Records for the Autonomous Video Editor. Use when changing architecture or evaluating major technology choices.
---

# Architecture and ADR

## Responsibilities
- Preserve the five-layer NLE architecture and its one-way dependency direction.
- Define interfaces between presentation, application core, real-time engines, media/data and platform adapters.
- Keep UI, decode, render, audio and AI workloads isolated by explicit message or command boundaries.
- Record consequential decisions as Architecture Decision Records (ADRs).

## Required ADR format
Each ADR must include: title, status, context, decision, alternatives considered, consequences, risks, validation plan and date. Store under `docs/adr/NNNN-short-title.md`.

## Architecture rules
- Native project model is canonical; OTIO is an interchange format.
- Media files are immutable source assets. Edits are non-destructive references plus operations.
- Project mutations are transactional commands.
- Use rational time (`numerator/denominator` or integer ticks with a declared timebase) for editorial timing. Do not use floating-point seconds as canonical timeline time.
- GPU resources are owned by the render subsystem and exposed through safe handles/fences, never raw UI ownership.
- Audio callback code must not allocate, block, log synchronously or acquire contended locks.
- Platform-specific APIs belong behind adapters.
- No network service or cloud inference dependency in the production path.

## Decision process
1. State the requirement and constraints.
2. Compare at least two viable options.
3. Check current official documentation and supported versions.
4. Build a small proof of concept for uncertain high-risk integrations.
5. Measure on target hardware.
6. Record the decision and revisit trigger.

Do not invent API capabilities. Mark unverified assumptions explicitly and create a spike task to validate them.
