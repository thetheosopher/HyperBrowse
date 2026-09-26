# Performance Scenario Matrix

The automated runner currently measures the scenarios below. Metrics not
listed here are not inferred from these measurements.

| Scenario | Dataset/workload | Current measurements | Execution class |
| --- | --- | --- | --- |
| Process startup and first visible thumbnail | A, small mixed folder | First-window visibility, visibility-to-first-thumbnail, process-to-first-thumbnail | Automated in Release CI |
| Persistent thumbnail-cache microbenchmark | `cache-micro-v1`, fixed synthetic workload | Disk-hit latency, store throughput, compaction, cache-worker queue delay, hidden-window scroll dispatch | Automated in Release CI |
| Large-folder enumeration, time to 50 thumbnails, metadata throughput | B/C | No stable measurement producer yet | Not instrumented; manual investigation only |
| Physical scroll hitches and selection responsiveness | B/C | No physical input or dropped-frame instrumentation | Manual/hardware-dependent |
| Viewer open, full decode, navigation, and prefetch effectiveness | A/D/E | No repeatable runner scenario yet | Not instrumented; manual investigation only |
| WIC versus nvJPEG format throughput | B/C | CI compiles both decode configurations; current benchmark does not decode the dataset | Not measured by the current runner |
| A6 WIC versus GPU thumbnail scaling | B/C/E, 256 px and largest thumbnail preset | No paired producer; decode, scale, upload, completion/readback, and cache-ready costs are not compared | Not implemented; blocked on reviewed D3/A8 baselines and verified hardware-GPU execution |
| RAW embedded-preview/full-decode behavior | D, locally staged manifest-backed NEF/NRW | Dataset staging and identity only; no automated decode measurement | Hardware/codec-dependent manual investigation |
| Peak/steady memory and CPU/GPU utilization | C/E | No stable sampling producer in the current suite | Manual/profiler-dependent |

Dataset A-E generation is reproducible. A dataset's `dataset.json` contains its
file inventory and SHA-256 values; generated binaries live under `build/` and
are not committed. Dataset D is available only when valid local inputs and a
completed `raw-manifest.json` are supplied. The template does not contain RAW
media.

## Baseline State

`baseline.json` currently preserves the existing absolute CI guardrails and
defines separate profiles for the two Release decode paths. Relative baselines
are deliberately marked `collecting`: this workspace cannot produce the ten
independent hosted workflow samples required for calibration. CI retains five
repetitions per scenario per workflow run for 90 days. Once ten successful
artifacts exist for each profile, review their distributions, update the
profile metrics and tolerances, mark it calibrated, and remove
`-AllowProvisionalBaseline` from the workflow invocation. Until then, the
absolute startup and cache limits remain active and no local measurement is
claimed as a hosted-runner baseline.

## A5/A6 Decision

The current reports do not measure WIC scale-path allocation pressure or a
paired CPU/WIC versus GPU scale comparison. That is missing evidence, not
evidence of either benefit or lack of benefit. Keep further A5 WIC pooling and
A6 GPU scaling deferred until those measurements are added and reviewed. The
A6 execution and go/no-go contract is in
[Prompt 03](../../specs/FUTURE-ROADMAP-PROMPT-03-A6-GPU-THUMBNAIL-SCALING.md).
The hosted collection and paired hardware run sequence is in
[Prompt 04](../../specs/FUTURE-ROADMAP-PROMPT-04-HOSTED-EVIDENCE-AND-A6-PAIRED-BENCHMARK.md).
