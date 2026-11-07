---
name: edit-plan-schema
description: Defines, validates and evolves the editor's typed EditPlan schema used by AI models, importers, scripts and the command bus to describe non-destructive edits.
---

# EditPlan Schema

## Purpose
EditPlan is a versioned, declarative proposal format. It is not an arbitrary executable script and not the complete native project format.

## Required design
- Schema version and producer metadata.
- Project/sequence identity and target timebase.
- Stable source asset IDs, never untrusted filesystem commands.
- Track/clip placement with rational time values.
- Source in/out ranges, speed mapping and transforms.
- Typed transitions and effect nodes with validated parameters.
- Audio gain/fades/routing.
- Captions/text references and style tokens.
- Explicit assumptions, warnings and confidence.
- Validation report and optional user-approval requirements.

## Validation
Reject unknown required node types, invalid IDs, negative or out-of-range media references, impossible durations, invalid rational denominators, unsupported effects and excessive resource requests. Unknown optional fields may be preserved only when forward-compatibility rules allow.

## Evolution
Use semantic schema versions and migration tests. Never silently reinterpret existing fields. Maintain JSON Schema or an equivalent machine-readable contract plus C++ types and Python types generated or tested for parity.

## Acceptance
Golden fixtures, malformed-input tests, version migration, C++/Python parity and round-trip serialization.
