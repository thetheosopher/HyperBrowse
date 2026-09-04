# ADR 0002: Rendering Caches Use Content Identity

- Status: Accepted
- Date: 2026-09-04

## Context

Browser and viewer collections change while users navigate and delete files. Removing an item can cause a later file to reuse the same numeric index. A render cache keyed only by that index can therefore present a bitmap belonging to a different file.

## Decision

Render and image caches must be invalidated explicitly when collection mutation can reuse an index. Where practical, cache state should be associated with file/content identity and relevant decode options. After item removal, a stale bitmap sentinel must be reset before the next paint; an unchanged numeric index is not evidence that the cached content remains valid.

Asynchronous viewer loads should preserve the last valid image while the replacement is decoding unless the product behavior explicitly requires a blank/loading state.

## Consequences

- Delete, insert, reorder, and folder-reload paths need explicit cache invalidation review.
- Tests should include removal where the next item takes the deleted item's old index.
- Viewer transitions remain visually stable during cache misses.

## Rejected alternative

Checking whether the current index changed was rejected because index reuse is a normal result of vector erasure and collection reconciliation.
