# HyperBrowse Specifications

HyperBrowse is a native Windows image browser and viewer. The current release
is 2.8.0.

## Product and Roadmap Authority

- [PRODUCT_SPEC.md](PRODUCT_SPEC.md) is the authoritative product contract for
  behavior and formats in the current released product.
- [FUTURE-ROADMAP.md](FUTURE-ROADMAP.md) is the single forward-looking list of
  proposed work, priorities, and current roadmap status. The roadmap does not
  change the shipped contract until a release is published.
- [HyperBrowse-Bug-Reports 2.md](<HyperBrowse-Bug-Reports%202.md>) is the
  source issue report.
- [HyperBrowse-Bug-Reports-2-Review-Plan.md](HyperBrowse-Bug-Reports-2-Review-Plan.md)
  maps those issue numbers to implementation and verification evidence. It is
  an issue audit, not a second product backlog; accepted open work belongs in
  the roadmap.

Nothing in a roadmap or execution brief is evidence that a feature shipped.

## Execution Briefs and Evidence Ledgers

The numbered prompts contain implementation detail and execution evidence for
work linked from the roadmap. The roadmap controls current priority and status;
prompt ledgers are not independent backlogs or product contracts.

- [Prompt 01 — Validation Baseline](FUTURE-ROADMAP-PROMPT-01-VALIDATION-BASELINE.md)
- [Prompt 02 — Performance Evidence](FUTURE-ROADMAP-PROMPT-02-PERFORMANCE-EVIDENCE.md)
- [Prompt 03 — A6 GPU Thumbnail Scaling](FUTURE-ROADMAP-PROMPT-03-A6-GPU-THUMBNAIL-SCALING.md)
- [Prompt 04 — Hosted Evidence and A6 Paired Benchmark](FUTURE-ROADMAP-PROMPT-04-HOSTED-EVIDENCE-AND-A6-PAIRED-BENCHMARK.md)
- [Prompt 05 — N-Up Compare](FUTURE-ROADMAP-PROMPT-05-N-UP-COMPARE-AND-ZOOM-SYNC.md)
- [Prompt 06 — Performance UI Surfaces](FUTURE-ROADMAP-PROMPT-06-PERFORMANCE-UI-SURFACES.md)
- [Prompt 07 — Color-Managed Display](FUTURE-ROADMAP-PROMPT-07-COLOR-MANAGED-DISPLAY-PATH.md)
- [Prompt 08 — Workflow and Format Enhancements](FUTURE-ROADMAP-PROMPT-08-WORKFLOW-AND-FORMAT-ENHANCEMENTS.md)
- [Prompt 09 — Code Quality Hygiene](FUTURE-ROADMAP-PROMPT-09-CODE-QUALITY-HYGIENE.md)
- [Prompt 10 — UI Design Polish](FUTURE-ROADMAP-PROMPT-10-UI-DESIGN-POLISH.md)

## Current Implementation References

- [../docs/architecture.md](../docs/architecture.md) describes component
  ownership, threading, rendering, cache, and lifetime boundaries.
- [../docs/testing.md](../docs/testing.md) describes build, smoke-test,
  benchmark, diagnostics, and validation expectations.
- [../docs/user-guide.html](../docs/user-guide.html) is the user-facing guide
  for the current application.
- [../docs/decisions/README.md](../docs/decisions/README.md) indexes durable
  architectural decisions and invariants.
- [../docs/perf/scenario-matrix.md](../docs/perf/scenario-matrix.md) defines
  performance scenarios; [../docs/perf/latest.md](../docs/perf/latest.md) is
  the last generated benchmark report. Check its run date and revision before
  treating it as current evidence; it is not itself a calibrated baseline.

Active engineering plans provide implementation detail but do not override the
product spec or roadmap:

- [../docs/UI_LAYOUT_CONSISTENCY_PLAN.md](../docs/UI_LAYOUT_CONSISTENCY_PLAN.md)
- [../docs/keyboard-accessibility-plan.md](../docs/keyboard-accessibility-plan.md)
- [../docs/keyboard-shortcuts-plan.md](../docs/keyboard-shortcuts-plan.md)
- [../docs/THEMED_DIALOG_PLAN.md](../docs/THEMED_DIALOG_PLAN.md)

## Authority Order

When documents disagree, use this order:

1. Current source code and tests.
2. [PRODUCT_SPEC.md](PRODUCT_SPEC.md) for released behavior.
3. Current architecture, testing, and user-guide documents.
4. [FUTURE-ROADMAP.md](FUTURE-ROADMAP.md) for proposed work and its status.
5. Execution briefs and engineering plans for implementation detail only.
6. [archive/README.md](archive/README.md) for historical context.

## Archive

[archive/README.md](archive/README.md) indexes historical specifications,
completed plans, and reviews. Archived documents preserve rationale and
evidence but are not maintained as current requirements.
