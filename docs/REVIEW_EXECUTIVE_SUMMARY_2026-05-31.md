Review conducted: 2026-05-31
Reviewer: Claude Opus (via GitHub Copilot)
Perspectives: Software Optimization Engineering + Product Management
Codebase: HyperBrowse (native Windows image browser/viewer)

# Executive Summary

**What it is.** HyperBrowse is a native Windows (10/11, x64) image **browser and viewer** built in
modern C++20 with Direct2D rendering, WIC/LibRaw/optional-nvJPEG decoding, asynchronous folder and
thumbnail pipelines, bounded memory caches, and a persistent on-disk thumbnail cache. It ships as a
portable zip and an installer. The codebase is mature and well-engineered: consistent RAII, careful
error handling, cancellation-aware schedulers, and a purpose-built thread pool. This is a healthy,
production-quality project — the recommendations below are optimizations and growth, not rescues.

## Top 3 Performance / Reliability Risks

1. **Redundant work on the thumbnail hot path.** The thumbnail cache key normalized every file path
   (heap allocation + full-string lowercasing) on *every* hash and *every* equality comparison —
   work repeated for every visible thumbnail on every scroll. *(Fixed in this pass with
   allocation-free comparison/hashing.)*
2. **No performance regression gate.** The app already measures startup and first-paint timings, but
   nothing in CI fails when a commit regresses them — the core "fast browsing" value prop is
   unprotected. Recommended as the highest-leverage next investment.
3. **Thread churn from `std::async`.** Several background services still spawn a fresh OS thread per
   task instead of using the existing bounded pool, which can transiently oversubscribe the CPU
   during rapid folder switching.

## Top 3 Product Opportunities

1. **Cross-folder search & faceted filtering.** Today search/filter is limited to filenames in the
   current folder, even though ratings, tags, and metadata are already captured. Letting users find
   images across folders by name, rating, tag, or date is the most-requested capability in this class
   of tool and reuses data the app already has.
2. **Rating/tag filters and saved views.** Users can set ⭐ ratings and tags but cannot filter to,
   e.g., "≥4 stars" — a low-effort, high-value addition over existing data.
3. **Surface decode-failure reasons.** Failed thumbnails currently show a generic placeholder; the
   app already records *why* a decode failed, so exposing it on hover would noticeably improve
   troubleshooting.

## Single Highest-Leverage Action

**Add a startup/first-paint performance regression gate to CI**, built on the benchmark the app
already emits. HyperBrowse's entire value proposition is speed; protecting it with an automated gate
costs little, prevents silent regressions across the whole roadmap, and makes every future
optimization measurable. (The hot-path cache fix delivered in this review is exactly the kind of gain
such a gate would lock in.)

See `CODE_REVIEW_2026-05-31.md` for full findings and `ENHANCEMENT_ROADMAP_2026-05-31.md` for the
prioritized plan.
</content>
