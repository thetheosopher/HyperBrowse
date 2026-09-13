# HyperBrowse Specifications

HyperBrowse is a native Windows image browser and viewer. The current release
is 2.3.0.

## Start Here

- [PRODUCT_SPEC.md](PRODUCT_SPEC.md) is the authoritative product contract for
	shipped behavior, supported formats, workflow boundaries, and product
	invariants.
- [FUTURE-ROADMAP.md](FUTURE-ROADMAP.md) is the single forward-looking list of
	deferred features, experiments, performance work, and explicit non-goals.

Nothing in the roadmap or archive is evidence that a feature is shipped.

## Current Implementation Documents

- [../docs/architecture.md](../docs/architecture.md) describes component
	ownership, threading, rendering, cache, and lifetime boundaries.
- [../docs/testing.md](../docs/testing.md) describes build, smoke-test,
	benchmark, diagnostics, and manual validation expectations.
- [../docs/user-guide.html](../docs/user-guide.html) is the user-facing guide
	for the current application.
- [../docs/decisions/README.md](../docs/decisions/README.md) indexes durable
	architectural decisions and invariants.

The following documents are active engineering work plans. They may describe
unfinished implementation work, but they do not override the product contract:

- [../docs/UI_LAYOUT_CONSISTENCY_PLAN.md](../docs/UI_LAYOUT_CONSISTENCY_PLAN.md)
- [../docs/keyboard-accessibility-plan.md](../docs/keyboard-accessibility-plan.md)
- [../docs/THEMED_DIALOG_PLAN.md](../docs/THEMED_DIALOG_PLAN.md)

## Authority Order

When documents disagree, use this order:

1. Current source code and tests.
2. [PRODUCT_SPEC.md](PRODUCT_SPEC.md).
3. Current architecture, testing, and user-guide documents.
4. [FUTURE-ROADMAP.md](FUTURE-ROADMAP.md) for work that has not shipped.
5. [archive/README.md](archive/README.md) for historical context only.

## Archive

[archive/README.md](archive/README.md) explains the historical documents moved
out of the primary path. Their content is retained for rationale and change
history, but they are not maintained as current requirements.
