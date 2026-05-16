# HyperBrowse Spec Pack

This folder contains a repo-ready planning pack for building a high-performance native Windows image browser and viewer using a low-overhead pure native approach.

## Included documents

- `01-product-spec.md`
- `02-architecture.md`
- `03-performance-strategy.md`
- `04-ui-behavior.md`
- `05-benchmarking-plan.md`
- `06-implementation-phases.md`
- `07-copilot-prompt-sequence.md`
- `08-issues-to-resolve.md`
- `09-hardening-pass.md`
- `10-prioritized-enhancements.md`
- `11-file-management-workflow.md`
- `12-visual-polish-pass.md`
- `13-branding-and-ui-assessment.md`
- `14-todo.md`
- `15-d2d-rendering-migration.md`
- `16-toolbar-ux-redesign.md`

## Rendering stack

- Win32 shell
- Direct2D and DirectWrite rendering for the thumbnail grid and viewer; double-buffered GDI for the toolbar strip and details mode
- Per-monitor DPI awareness v2
- WIC for baseline common-format decoding
- LibRaw for supported mainstream RAW formats (in-process or out-of-process)
- optional nvJPEG for accelerated JPEG thumbnail batches

## Suggested next step

Start with `07-copilot-prompt-sequence.md` and run the prompts sequentially against an empty repository initialized with CMake.
