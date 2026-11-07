---
name: ai-feedback-learning
description: Captures explicit user edits and ratings as consent-based feedback, creates reviewed training examples, tracks model versions and prevents feedback contamination or accidental online learning.
---

# AI Feedback and Learning

## Rules
- Never silently use private user media or edits for training.
- Ask for explicit opt-in and record consent scope.
- Separate product telemetry from training data.
- Capture before/after EditPlan diffs and relevant context only when permitted.
- Exclude personal or sensitive content not needed for editing decisions.
- Provide provenance, license, deletion and retention metadata.
- Human-review candidate examples before promoting them into trusted training data.
- Version datasets and models; retain reproducible evaluation reports.
- Do not update production model weights automatically from a single user action.

## Acceptance
Consent tests, opt-out behavior, provenance audit, dataset removal workflow and held-out evaluation before model promotion.
