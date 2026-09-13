# Archived Plans and Reviews

This directory contains historical specifications, implementation plans, code
reviews, release ledgers, and prompts that are no longer current requirements.
They are retained for design rationale, completed-work history, and future
reference. They do not override source code, tests, or the current documents
linked from [../README.md](../README.md).

## Current Authority

- [../PRODUCT_SPEC.md](../PRODUCT_SPEC.md) is the current shipped product
  contract.
- [../FUTURE-ROADMAP.md](../FUTURE-ROADMAP.md) is the current home for
  unimplemented product ideas and deferred engineering work.
- [../../docs/architecture.md](../../docs/architecture.md) and
  [../../docs/testing.md](../../docs/testing.md) describe current architecture
  and validation policy.

## Archived Specification Pack

The original numbered planning pack has been retired as a primary navigation
system. Its contents remain here as historical context:

- `02-architecture.md`, `03-performance-strategy.md`, and
  `05-benchmarking-plan.md` describe the original architecture, performance,
  and benchmark plans.
- `04-ui-behavior.md` and `11-file-management-workflow.md` describe earlier
  UI and file-workflow contracts.
- `06-implementation-phases.md`, `07-copilot-prompt-sequence.md`,
  `08-issues-to-resolve.md`, `09-hardening-pass.md`, and
  `17-release-2.0-hardening-plan.md` record implementation sequencing and
  release hardening.
- `10-prioritized-enhancements.md`, `12-visual-polish-pass.md`,
  `13-branding-and-ui-assessment.md`, `15-d2d-rendering-migration.md`,
  `16-toolbar-ux-redesign.md`, and `fr1-culling.md` record completed or
  superseded enhancement work.

`01-product-spec.md` and `14-todo.md` were renamed to the canonical
`PRODUCT_SPEC.md` and `FUTURE-ROADMAP.md` documents rather than duplicated here.

## Archived Reviews and Engineering Plans

The following documents were moved here from `docs/` because they record dated
reviews, completed release work, or implementation plans that no longer guide
the current repository:

- `CODE_REVIEW_2026-05-31.md`
- `CODE_REVIEW_2026-07-30.md`
- `ENHANCEMENT_ROADMAP_2026-05-31.md`
- `ENHANCEMENT_ROADMAP_2026-07-30.md`
- `REVIEW_EXECUTIVE_SUMMARY_2026-05-31.md`
- `REVIEW_EXECUTIVE_SUMMARY_2026-07-30.md`
- `RELEASE_POLISH_PLAN_2026-09-08.md`
- `HyperBrowse-2.0-Feedback.md`
- `mainwindow-clean-code-plan.md`
- `THUMBNAIL_PERFORMANCE_PLAN.md`
- `UI_LAYOUT_CONSISTENCY_IMPLEMENTATION_PROMPT.md`

The active engineering plans remain in `docs/` and are listed from
[../README.md](../README.md). They are working documents for unfinished tasks,
not product specifications.

## Handling Archived Links

Some archived documents contain links to source files or older document names
that were correct when the document was written. Treat those links as historical
pointers. For current behavior, follow the authority order in
[../README.md](../README.md) and use the repository history when the original
context matters.
