# ADR-0018: Deterministic Beat-Grid Planner and Hardened Proposal Bridge

## Context
Integrating artificial intelligence into a professional non-linear video editor introduces significant hazards:
1. **Model Hallucination & Timing Drift**: Unconstrained language or vision models can emit impossible edit timestamps, floating-point rounding drifts, or overlap clips illegally.
2. **Untrusted Metadata & Injection**: Media file names, transcripts, OCR text, and user prompt text are untrusted inputs that must never directly trigger arbitrary mutations.
3. **Destructive Mutation Risks**: Direct state mutation by AI models bypasses undo/redo history and transactional integrity.
4. **Poor Undo Experience**: If an AI edit proposal executes as dozens of separate small clip insertions, reverting the AI edit requires the user to press Undo dozens of times.

## Decision
We implemented a two-part architecture: the **Deterministic Planner** (`ave::DeterministicPlanner`) and the **Hardened Proposal Bridge** (`ave::ProposalBridge`).

1. **Deterministic Beat-Grid Planner (`ave::DeterministicPlanner`)**:
   - Takes musical rhythm (`ave::AudioRhythmReport`), reference fingerprint (`ave::ReferenceFingerprint`), and candidate clips (`ave::SourceClipCandidate`).
   - Generates candidate cut points strictly anchored to exact musical beats and measure downbeats.
   - Computes composite costs for candidate clips across motion match, visual similarity, repeat prevention penalty, and speech boundary safety.
   - Emits structured `ave::EditProposalEnvelope` instances conforming to `schemas/edit-proposal-envelope.schema.json`.
   - Every operation includes `ProposalEvidence` (justified by beat/downbeat/reference match, exact timestamp, model version, and confidence).

2. **Hardened Proposal Bridge (`ave::ProposalBridge`)**:
   - **Floats Stop at the Boundary**: All timestamps crossing into the proposal envelope snap strictly to exact 64-bit rational time (`ave::Rational`).
   - **Semantic Precondition Check**: Validates that target tracks and referenced media assets exist, and durations are positive and non-overlapping.
   - **Cloned Dry-Run Simulation**: Simulates the proposal on an isolated deep clone of `ave::Timeline` and `ave::MediaPool`. Generates the candidate timeline's fingerprint $F_{\text{candidate}}$ and calculates the stylistic distance $D(F_{\text{ref}}, F_{\text{candidate}})$ without touching live project state.
   - **Single Atomic Transaction (`ave::CompositeCommand`)**: When approved, all child operations are grouped into a single atomic `CompositeCommand` with `CommandOrigin::AI`.
   - **1-Step Undo/Redo**: Reverting or re-applying an entire multi-clip AI edit takes exactly **one single Undo step**. If any child command fails during execution, all previously applied operations roll back immediately.

## Consequences
- AI intelligence is completely decoupled from project state mutation: the planner emits proposals, never commands.
- Guaranteed zero timing drift and zero invalid timeline states.
- Seamless, predictable user experience: 1-step undo completely reverts AI suggestions.
- Verified by automated test suite `ave_planner_test` passing 100% across all 23 project test suites.
