# Layer 2 — Application Core: AI Subsystem

## Responsibilities
- AI Orchestrator: receives natural-language user requests, references, and candidate footage.
- Local Intent-to-Instruction Transformer: maps requests to typed editing intents.
- Temporal Editing Policy Transformer: outputs non-destructive `EditPlan` proposals.
- Local IPC: Protobuf/named pipes communication with isolated local AI inference worker.
- Semantic Proposal Validator: validates all ranges, asset IDs, and operations against the current project revision before presenting to user.

## Boundary Rules
- The AI never has direct pointers into the Project Model and never executes arbitrary shell commands.
- All AI operations are bounded, declarative proposals applied strictly through the Command Bus as undoable transactions.

