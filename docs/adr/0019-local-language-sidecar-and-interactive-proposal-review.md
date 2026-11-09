# ADR-0019: Local Language Sidecar, Prompt Constraint Parser, and Interactive Proposal Inspector

## Context
Enabling natural language interaction in a professional video editor introduces critical architectural challenges:
1. **Crash Isolation**: High-parameter language and vision-language models (e.g. Qwen3-VL, llama.cpp GGUF) have unpredictable VRAM and memory footprints. Running them in-process risks crashing the entire native NLE if an out-of-memory (OOM) condition or CUDA driver crash occurs.
2. **Prompt Injection Protection**: Video transcripts, OCR data, file paths, and free-text user prompts are untrusted inputs. If an LLM directly generates edit actions or shell commands, it creates an attack vector.
3. **Deterministic Bridge**: Language models must never directly edit the timeline. Instead, they must translate natural language into structured mathematical constraints (`PlannerConfig` and target `ReferenceFingerprint`) that feed into the deterministic Phase 7F solver.
4. **User Review & Approval**: Editors must be able to visually inspect proposed edits, see why each cut was made (musical beat, downbeat, speech pause), view fidelity against the reference style ($D(F_{\text{ref}}, F_{\text{target}})$), and commit the entire edit in a single 1-step undoable transaction.

## Decision
We implemented a three-tier local language and proposal review architecture:

1. **Out-of-Process Language Sidecar (`ave::LanguageSidecar`)**:
   - Manages an isolated worker process for heavy model inference.
   - Decoupled via local IPC with timeout protection (5000 ms).
   - Complete crash isolation: if the sidecar crashes or hangs, the native editor remains 100% operational and seamlessly falls back to the local rule-based semantic parser.

2. **Prompt Constraint Parser (`ave::PromptParser`)**:
   - Parses user requests (e.g. *"Create a fast energetic 15s action montage cut to the drum beats, preserve speech"*) into typed `ParsedPromptConstraints`.
   - Extracts exact `Rational` durations, pacing intent (`Fast`, `Medium`, `Slow`, `Dynamic`), beat/downbeat synchronization, target motion energy, and speech preservation flags.
   - Synthesizes `PlannerConfig` weights and target `ReferenceFingerprint` parameters for `ave::DeterministicPlanner`.

3. **Interactive Proposal Inspector (`ave::ProposalInspector`)**:
   - Formats proposals and dry-run outcomes into a structured presentation ViewModel (`ave::ProposalInspectionViewModel`).
   - Calculates stylistic fidelity percentage ($100 \times (1.0 - \text{distance})$).
   - Provides per-clip diff entries showing track destination, timestamps, and justification.
   - Connects directly to `ave::ProposalBridge` for 1-click atomic commit and 1-step undo/redo.

## Consequences
- Complete crash and memory safety: the editor core cannot be brought down by model failures.
- Natural language prompts produce deterministic, mathematically optimal edit plans.
- Full human-in-the-loop review and approval before timeline mutation.
- Verified by automated test suite `ave_language_layer_test` passing 100% across all 24 project test suites.

