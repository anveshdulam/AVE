---
name: documentation-and-handoff
description: Maintains architecture documentation, subsystem READMEs, API contracts, build instructions, ADRs, changelogs, milestone reports and Antigravity handoffs.
---

# Documentation and Handoff

## Required documentation
- Root README: purpose, supported status, build prerequisites and quick start.
- Architecture overview and dependency diagram.
- ADRs for consequential decisions.
- Per-subsystem README with responsibility, interfaces, threading, errors and tests.
- Build and troubleshooting guide.
- Project data/schema documentation.
- Milestone acceptance checklist.
- Known limitations and compatibility matrix.

## Rules
- Documentation must match the code; do not describe planned features as implemented.
- Label status: planned, experimental, implemented, tested, production-ready.
- Include exact commands and tested tool versions.
- Update docs in the same change as interface or behavior changes.
- Record unresolved assumptions and follow-up tasks.
- At handoff, list changed files, build/test output, known issues and next smallest task.

## Antigravity memory
Maintain `docs/PROJECT_STATUS.md` as the durable source of truth. Update it after each completed milestone or significant change. Do not rely on chat history as project state.
