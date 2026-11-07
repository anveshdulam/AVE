---
name: color-management
description: Implements color science and grading workflows using OpenColorIO and ACES-compatible configuration, transforms, LUTs, scopes, HDR metadata and color-correct rendering.
---

# Color Management

## Principles
Color is a managed pipeline, not a collection of arbitrary RGB sliders. Every image stage must declare its color space, transfer function, primaries, range and bit depth where known.

## Responsibilities
- Integrate OpenColorIO using a versioned configuration.
- Define project working space and display/view transforms.
- Preserve source color metadata from media ingest.
- Define SDR and HDR output paths explicitly.
- Support LUT loading with validation and clear input/output space expectations.
- Build grading nodes and parameter serialization.
- Implement scopes from well-defined signal stages.

## Rules
- Never silently treat unknown color metadata as a known standard.
- Do not apply display transforms twice.
- Keep scene-referred and display-referred operations distinct.
- Validate GPU shader transforms against OCIO CPU reference results.
- Document precision, gamut clipping and alpha handling.
- Treat ACES as a configurable color-management workflow, not a magic “cinematic look.”

## Acceptance
Reference chart tests, transform parity tests, round-trip project tests and documented behavior for unknown metadata, SDR, HDR and LUT errors.
