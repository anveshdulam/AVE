---
name: command-bus-undo
description: Implements the application command bus, typed edit commands, validation, transactional project mutations, undo/redo, autosave events and safe AI command execution.
---

# Command Bus and Undo/Redo

## Purpose
Every mutation of canonical project state must pass through the command bus. UI tools, keyboard shortcuts, scripts and AI all use the same validated command interface.

## Command contract
A command should define:
- Stable command type/version.
- Target entity IDs.
- Preconditions and validation.
- Apply behavior.
- Revert/undo behavior or an explicit compensating operation.
- Result and structured error.
- Audit metadata and origin (user, AI, import, migration).

## Rules
- Validate entity existence, media bounds, track constraints and timebase consistency before mutation.
- Apply multi-step edits transactionally; either all changes commit or none do.
- Group related edits into a single undoable transaction.
- Avoid storing huge media buffers in undo history; store reversible metadata operations.
- Persist command history only if recovery semantics are clearly defined.
- AI proposals require preview/approval policy; never auto-commit destructive or ambiguous changes.
- Commands must be deterministic where possible and idempotent only when explicitly designed.

## Acceptance
Tests prove apply/revert symmetry, transaction rollback, nested command grouping, stale IDs, invalid ranges, undo/redo and project-save consistency.
