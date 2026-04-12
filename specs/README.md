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

## Intended stack
- Win32 shell
- Direct2D + Direct3D11 rendering
- WIC for baseline common-format decoding
- LibRaw for Nikon RAW
- optional nvJPEG for accelerated JPEG thumbnail batches

## Suggested next step
Start with `07-copilot-prompt-sequence.md` and run the prompts sequentially against an empty repository initialized with CMake.
