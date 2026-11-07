---
name: ai-orchestration-ipc
description: Integrates local AI models with the native editor through versioned local IPC, task orchestration, structured proposals, validation, approvals and command-bus execution.
---

# AI Orchestration and IPC

## Boundary
AI is an application-core client. It may read approved project snapshots and media-analysis results, then propose typed edits. It cannot directly mutate the canonical project database, call internal engine pointers or execute arbitrary shell commands.

## Workflow
1. Receive user request and project context.
2. Resolve referenced media and permissions.
3. Request perception features.
4. Generate one or more structured EditPlan proposals.
5. Validate schema, media references, time ranges, track constraints, effect availability and resource budgets.
6. Present a preview/diff and approval requirements.
7. Convert approved proposal into command-bus transactions.
8. Execute and monitor jobs.
9. Request QA analysis and propose corrections.
10. Persist provenance and user feedback with consent.

## IPC
Choose local IPC only after a Windows proof of concept. Evaluate named pipes or local sockets with explicit authentication/ACLs, versioned message envelopes, bounded payloads, timeouts, cancellation and schema validation. Do not bind an unauthenticated network listener.

## Reliability
Support request IDs, correlation IDs, progress events, cancellation, retries only for idempotent operations, timeouts and clear error categories. Do not let a failed AI worker corrupt project state.

## Acceptance
AI can propose, preview, approve, apply, undo and audit a multi-command edit without bypassing the command bus.
