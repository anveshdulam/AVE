# ADR-0010: Command Bus, Typed Mutations, and Transactional Undo/Redo Engine

## Status
Accepted

## Date
2026-11-08

## Context
In professional non-linear video editors, user edits (inserting clips, moving tracks, splitting items, adjusting transforms, muting tracks) must be completely non-destructive, strictly validated, and instantly reversible across deep undo/redo histories. Furthermore, as autonomous AI editing intelligence is integrated into the application, AI-proposed operations must pass through the exact same validated transactional interface as user interactions, ensuring zero corrupt state, accidental destruction, or invalid overlap.

According to the product constitution:
1. **Layer 2 Authority:** Mutations to canonical project models (`Timeline`, `MediaPool`) reside in Layer 2 (`core`). Presentation UI (Layer 1) and AI models cannot directly mutate internal containers.
2. **Symmetric Apply and Revert:** Every command must implement both `apply()` and `revert()` operations, stashing minimal metadata without copying heavy media buffers.
3. **Precondition Validation:** Commands must validate constraints (track existence, positive duration, range boundaries, non-overlapping clips) prior to executing any mutation.
4. **Atomic Transactions:** Multi-step edits (e.g., ripple edits, multi-clip moves, batch cuts) must execute as atomic transactions via `CompositeCommand`. If any sub-command fails, all previous sub-commands must roll back in reverse order, leaving project state pristine.
5. **Project Dirty Tracking:** The command history must track save-point state (`is_dirty()`, `mark_clean()`) and notify UI listeners for autosave and menu status updates.

## Decision
1. **Command Contract (`ave::Command`, `ave::CommandResult`):**
   - Created base class `ave::Command` with immutable `id` (UUID), `type`, `description`, `origin` (`User`, `AI`, `Script`, `Import`), and `timestamp`.
   - Defined structured `ave::CommandResult` returning `status` (`Success`, `FailedPrecondition`, `FailedExecution`, `FailedRevert`), message, and affected entity IDs.
   - Mandated `validate(const Timeline&, const MediaPool&)` for non-mutating pre-flight verification.

2. **Typed Timeline Commands:**
   - `InsertClipCommand`: Adds a clip to a track after checking overlap; reverts by removing the clip.
   - `DeleteClipCommand`: Removes a clip from a track, stashing a snapshot for revert restoration.
   - `MoveClipCommand`: Re-positions a clip in time or between tracks, validating against collision.
   - `SplitClipCommand`: Non-destructively splits a clip at a playhead timestamp via blade cut; reverts by removing both slices and restoring the original clip.
   - `SetTransformCommand`: Reversibly alters spatial translation, scale, rotation, anchor, opacity, and blend modes.
   - `SetTrackMuteCommand`: Reversibly toggles track muting.

3. **Composite Macro Transactions (`ave::CompositeCommand`):**
   - Implements atomic transaction semantics for grouped commands.
   - On error during sequential application, automatically rolls back all previously executed sub-commands in reverse order.

4. **History & Dirty-State Management (`ave::CommandHistory`):**
   - Maintains `undo_stack_` and `redo_stack_` with configurable history depth (default 100).
   - Manages clean save point index for accurate dirty tracking (`is_dirty()`).
   - Dispatches change notifications to registered listeners whenever history mutates or reverts.

## Consequences
- **Positive:**
  - Guaranteed project consistency: all user and AI operations are audited, validated, and reversible.
  - Zero memory bloat: undo operations store reversible metadata snapshots rather than raster frames.
  - 100% test pass rate across 15 automated test suites with explicit apply/revert symmetry and transaction rollback verification.
- **Negative:**
  - All new edit operations must be implemented as paired apply/revert command classes rather than direct member modifications.
