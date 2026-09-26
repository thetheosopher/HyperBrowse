# HyperBrowse Performance Benchmark

- Run: `627a02ea04158d5ab9ca344e386f6620a7db729d-20260926T194132969Z-local-workstation-unknown-dbfd5bae`
- Revision: `627a02ea04158d5ab9ca344e386f6620a7db729d` (dirty: True)
- Profile: `local-workstation/unknown`
- Runner: local-workstation; Microsoft Windows 11 Pro; 13th Gen Intel(R) Core(TM) i7-13700K; Virtual Desktop Monitor
- Decode path: `unknown`; configuration: `Release`
- Baseline: not-requested (Baseline comparison was not requested.)
- Sample runs: 10; failed samples: 0

| Metric | Dataset | Samples | Min | Median | p95 | Max | Unit |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| startup.processToFirstWindowVisibleMs | A | 5 | 222.86 | 231.58 | 251.85 | 251.85 | ms |
| startup.firstWindowVisibleToFirstThumbnailPaintedMs | A | 5 | 201.09 | 206.53 | 209.58 | 209.58 | ms |
| startup.processToFirstThumbnailPaintedMs | A | 5 | 429.39 | 432.67 | 459.98 | 459.98 | ms |
| cache.cacheHitAverageMs | cache-micro-v1 | 5 | 0.48 | 0.51 | 0.74 | 0.74 | ms |
| cache.storeEntriesPerSecond | cache-micro-v1 | 5 | 432.8 | 488.49 | 506.85 | 506.85 | entries/s |
| cache.compactionMs | cache-micro-v1 | 5 | 14.22 | 14.87 | 15.57 | 15.57 | ms |
| cache.cacheWorkerQueueDelayAverageMs | cache-micro-v1 | 5 | 44.61 | 60.12 | 60.12 | 60.12 | ms |
| cache.cacheWorkerQueueDelayMaxMs | cache-micro-v1 | 5 | 46 | 62 | 62 | 62 | ms |
| cache.scrollMessagesPerSecond | cache-micro-v1 | 5 | 3193.48 | 3478.44 | 3491.12 | 3491.12 | messages/s |

## Dataset and State

- `A` (small-mixed): 200 files, inventory SHA-256 `a4df0b55a0cc0f8b30035f72777ba97575e565eb8576c6b1e9d07d7678d8a661`
- `cache-micro-v1` (synthetic-cache-workload):  files, inventory SHA-256 `0c1da8e503c1e60e4f57f1d2a7b316ed8bdc6ad8ea331f420b0cda3b35c8f4a7`
- Startup process state: Each sample launches a fresh process.
- OS file-cache state: uncontrolled; no cold-disk claim is made.

## A5/A6 Readiness

The current automated suite does not measure WIC scale-path allocation pressure or paired CPU/WIC versus GPU scale cost. A5 WIC pooling and A6 GPU scaling remain gated; this report cannot establish benefit or lack of benefit.

Raw samples and the complete report are retained under `C:\Projects\Applications\HyperBrowse\build\bench\627a02ea04158d5ab9ca344e386f6620a7db729d\627a02ea04158d5ab9ca344e386f6620a7db729d-20260926T194132969Z-local-workstation-unknown-dbfd5bae`.
