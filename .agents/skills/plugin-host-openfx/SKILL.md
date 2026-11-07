---
name: plugin-host-openfx
description: Designs a safe extensible effect plugin host, OpenFX integration, plugin discovery, parameter mapping, GPU/CPU execution, crash isolation and compatibility testing.
---

# Plugin Host and OpenFX

## Scope
The plugin host is an application-core subsystem. It discovers plugins, loads metadata, manages instances and maps plugin parameters to project state and render graph nodes.

## Rules
- Treat plugin binaries as untrusted third-party code.
- Define ABI, supported OpenFX version and platform policy.
- Never load arbitrary plugins in the audio callback or UI thread.
- Plan crash isolation; process isolation may be required for unreliable plugins.
- Validate plugin parameter ranges and state serialization.
- Handle missing plugins gracefully when reopening projects.
- Distinguish plugin availability from project compatibility.
- Define GPU context and resource sharing contract before supporting GPU plugins.
- Do not promise full OpenFX compliance until conformance tests pass.

## Acceptance
Plugin scan, load/unload, parameter persistence, missing-plugin recovery, crash behavior and representative compatibility tests.
