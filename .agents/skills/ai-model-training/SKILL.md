---
name: ai-model-training
description: Designs and trains custom editing-specific Transformer models from scratch using PyTorch, including tokenization, datasets, training loops, checkpoints, evaluation and local inference export.
---

# Custom AI Editing Model Training

## Objective
Train specialized editing models, not a general-purpose foundation LLM. The initial models map structured editing context and user instructions to validated editing plans.

## Model strategy
Start with small experimental models and baselines:
- Instruction-to-intent model.
- Feature/context-to-editing-policy model.
- Optional ranking model for alternative edit plans.

Do not commit to a parameter count until the dataset, GPU VRAM and baseline task complexity are measured. Do not claim that a small model understands arbitrary raw video; perception features are supplied by the separate perception pipeline.

## Dataset contract
Each example should include:
- Source/reference asset IDs and provenance.
- Feature extractor and version.
- User instruction.
- Editing context and constraints.
- Target EditPlan or decision labels.
- Human review status and annotator provenance.
- Split/group ID to prevent leakage across clips from the same source.

## Training requirements
- Establish rule-based and simple learned baselines first.
- Split by source video/project, not random adjacent frames.
- Track training/validation loss and task metrics.
- Save configs, seeds, code revision, data manifest and checkpoints.
- Support resume, early stopping and gradient accumulation.
- Validate output schema and constraints.
- Compare against baselines on unseen source material.
- Keep a held-out test set untouched.

## Safety and quality
No training data without rights/permission. Do not train on private media without explicit consent. Record dataset licenses and deletion provenance. Treat synthetic labels as weak supervision and require human-reviewed examples for quality claims.

## Acceptance
Reproducible run, documented dataset lineage, no train/test leakage, measurable baseline comparison, valid structured outputs and local inference benchmark.
