---
name: security-privacy-supply-chain
description: Reviews local IPC, plugin loading, media parsing, filesystem access, model assets, dependency provenance, privacy, secrets and software supply-chain risks.
---

# Security, Privacy and Supply Chain

## Threat model
Treat media files, plugin binaries, project files, model checkpoints, downloaded dependencies and IPC messages as untrusted inputs.

## Requirements
- Validate file paths and prevent path traversal.
- Avoid arbitrary shell execution and unsafe deserialization.
- Restrict plugin loading to configured locations and provide trust warnings.
- Validate IPC peer identity and permissions.
- Use bounded message sizes and resource limits.
- Keep logs free of media contents and sensitive paths where possible.
- Store no API keys; production AI must not require external keys.
- Pin dependencies and record hashes/provenance for model weights.
- Scan dependency licenses and known vulnerabilities.
- Provide explicit consent before using user projects for training.
- Keep user media local by default and document all generated caches.

## Acceptance
Threat model, dependency/license inventory, IPC access tests, malformed project/media tests and documented data-retention behavior.
