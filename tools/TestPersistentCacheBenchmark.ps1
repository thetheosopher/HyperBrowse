[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$BuildDir = '',
    [string]$Configuration = 'Release',
    [string]$ExecutablePath = '',
    [string]$OutputPath = '',
    [double]$CacheHitAverageBudgetMs = 10,
    [double]$MinimumStoreEntriesPerSecond = 100,
    [double]$CompactionBudgetMs = 5000,
    [double]$CacheWorkerQueueDelayMaxBudgetMs = 500,
    [double]$MinimumScrollMessagesPerSecond = 250
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $PSCommandPath }
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Split-Path -Parent $scriptRoot
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $ProjectRoot 'build'
}
if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $ExecutablePath = Join-Path (Join-Path $BuildDir 'tests') (Join-Path $Configuration 'HyperBrowsePerformanceBenchmark.exe')
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $BuildDir 'persistent-cache-benchmark.json'
}

$ProjectRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$ExecutablePath = [System.IO.Path]::GetFullPath($ExecutablePath)
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)

if (-not (Test-Path $ExecutablePath)) {
    throw "Persistent-cache benchmark executable was not found: $ExecutablePath"
}

Remove-Item -Path $OutputPath -ErrorAction SilentlyContinue
& $ExecutablePath '--output' $OutputPath
if ($LASTEXITCODE -ne 0) {
    throw "Persistent-cache benchmark failed with exit code $LASTEXITCODE."
}
if (-not (Test-Path $OutputPath)) {
    throw "Persistent-cache benchmark output was not created: $OutputPath"
}

$metrics = Get-Content -Path $OutputPath -Raw | ConvertFrom-Json
$cacheHitAverageMs = [double]$metrics.cacheHitAverageMs
$storeEntriesPerSecond = [double]$metrics.storeEntriesPerSecond
$compactionMs = [double]$metrics.compactionMs
$cacheWorkerQueueDelayAverageMs = [double]$metrics.cacheWorkerQueueDelayAverageMs
$cacheWorkerQueueDelayMaxMs = [double]$metrics.cacheWorkerQueueDelayMaxMs
$scrollMessagesPerSecond = [double]$metrics.scrollMessagesPerSecond

if ($cacheHitAverageMs -gt $CacheHitAverageBudgetMs) {
    throw "cacheHitAverageMs exceeded its budget. Actual: $cacheHitAverageMs ms. Budget: $CacheHitAverageBudgetMs ms."
}
if ($storeEntriesPerSecond -lt $MinimumStoreEntriesPerSecond) {
    throw "storeEntriesPerSecond fell below its floor. Actual: $storeEntriesPerSecond. Minimum: $MinimumStoreEntriesPerSecond."
}
if ($compactionMs -gt $CompactionBudgetMs) {
    throw "compactionMs exceeded its budget. Actual: $compactionMs ms. Budget: $CompactionBudgetMs ms."
}
if ($cacheWorkerQueueDelayMaxMs -gt $CacheWorkerQueueDelayMaxBudgetMs) {
    throw "cacheWorkerQueueDelayMaxMs exceeded its budget. Actual: $cacheWorkerQueueDelayMaxMs ms. Budget: $CacheWorkerQueueDelayMaxBudgetMs ms."
}
if ($scrollMessagesPerSecond -lt $MinimumScrollMessagesPerSecond) {
    throw "scrollMessagesPerSecond fell below its floor. Actual: $scrollMessagesPerSecond. Minimum: $MinimumScrollMessagesPerSecond."
}

Write-Host "persistent_cache_benchmark_json=$OutputPath"
Write-Host "cache_hit_average_ms=$cacheHitAverageMs"
Write-Host "store_entries_per_second=$storeEntriesPerSecond"
Write-Host "compaction_ms=$compactionMs"
Write-Host "cache_worker_queue_delay_average_ms=$cacheWorkerQueueDelayAverageMs"
Write-Host "cache_worker_queue_delay_max_ms=$cacheWorkerQueueDelayMaxMs"
Write-Host "scroll_messages_per_second=$scrollMessagesPerSecond"

if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_STEP_SUMMARY)) {
    @(
        '## Persistent Cache Benchmark',
        '',
        "- Cache hit average: $cacheHitAverageMs ms",
        "- Store throughput: $storeEntriesPerSecond entries/s",
        "- Compaction: $compactionMs ms",
        "- Cache-worker queue delay: $cacheWorkerQueueDelayAverageMs ms average, $cacheWorkerQueueDelayMaxMs ms max",
        "- Scroll dispatch throughput: $scrollMessagesPerSecond messages/s",
        "- Snapshot: $OutputPath"
    ) | Add-Content -Path $env:GITHUB_STEP_SUMMARY
}
