<#
.SYNOPSIS
Advisory local performance check: run the benchmarks and compare medians with a per-machine baseline.

.DESCRIPTION
Wraps tools/RunBenchmarks.ps1 (Release, dataset A, startup + persistent cache) and compares the
median of each metric with docs/perf/local-baseline.json for this computer. Results are advisory:
absolute and relative timings are machine-specific, so a baseline recorded on one computer says
nothing about another. The startup scenario launches the app and needs an unlocked interactive desktop.

  -UpdateBaseline  Record this run as the baseline for this computer (explicit, never automatic).
  -ReportPath      Compare or record an existing report.json instead of running the benchmarks.
  -TolerancePercent  Allowed regression per metric (default 30).
  -Strict          Exit 1 when a metric regresses (default: report only).

This does not use or dispatch hosted CI; docs/perf/baseline.json (the CI calibration file) is untouched.
#>
[CmdletBinding()]
param(
    [switch]$UpdateBaseline,
    [string]$ReportPath = '',
    [int]$Runs = 5,
    [double]$TolerancePercent = 30,
    [double]$MillisecondNoiseFloor = 2,
    [switch]$NoBuild,
    [switch]$Strict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $PSCommandPath }
$root = [System.IO.Path]::GetFullPath((Split-Path -Parent $scriptRoot))
$baselinePath = Join-Path $root 'docs\perf\local-baseline.json'
$machine = $env:COMPUTERNAME
$datasetId = 'A'
Set-Location $root

if (-not $ReportPath) {
    if (-not $NoBuild) {
        cmake --preset vs2026-x64
        if ($LASTEXITCODE -ne 0) { throw 'Configure failed.' }
        cmake --build --preset release --target HyperBrowse HyperBrowseTests HyperBrowsePerformanceBenchmark
        if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
    }
    $generator = Join-Path $scriptRoot 'GenerateBenchmarkDatasets.ps1'
    try { & $generator -ProjectRoot $root -DatasetId $datasetId -VerifyOnly | Out-Null }
    catch { & $generator -ProjectRoot $root -DatasetId $datasetId | Out-Null }

    $started = Get-Date
    $markdown = Join-Path $root 'build\bench\local-latest.md'
    & (Join-Path $scriptRoot 'RunBenchmarks.ps1') -ProjectRoot $root -BuildDir (Join-Path $root 'build') `
        -Configuration Release -Scenario All -DatasetId $datasetId -Runs $Runs -MarkdownPath $markdown
    if ($LASTEXITCODE -ne 0) { throw "RunBenchmarks.ps1 failed (exit code $LASTEXITCODE)." }
    $latest = Get-ChildItem -Path (Join-Path $root 'build\bench') -Recurse -Filter report.json |
        Where-Object { $_.LastWriteTime -ge $started } | Sort-Object LastWriteTime | Select-Object -Last 1
    if (-not $latest) { throw 'No report.json was produced by RunBenchmarks.ps1.' }
    $ReportPath = $latest.FullName
}

$report = Get-Content -LiteralPath $ReportPath -Raw | ConvertFrom-Json
if ($report.failureCount -gt 0) { throw "The report records $($report.failureCount) failed sample(s); not comparing." }
$metrics = @($report.aggregate.metrics)
if ($metrics.Count -eq 0) { throw 'The report contains no metrics.' }

$baseline = [ordered]@{
    schemaVersion = 1
    note          = 'Advisory, machine-specific medians recorded by tools/CheckPerf.ps1 -UpdateBaseline. Do not compare across computers.'
    machines      = [ordered]@{}
}
if (Test-Path -LiteralPath $baselinePath) {
    $existing = Get-Content -LiteralPath $baselinePath -Raw | ConvertFrom-Json
    foreach ($prop in $existing.machines.PSObject.Properties) { $baseline.machines[$prop.Name] = $prop.Value }
}

if ($UpdateBaseline) {
    $recorded = [ordered]@{}
    foreach ($m in ($metrics | Sort-Object id)) {
        $recorded[$m.id] = [ordered]@{ median = [double]$m.median; unit = $m.unit; direction = $m.direction }
    }
    $baseline.machines[$machine] = [ordered]@{
        recordedUtc   = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        revision      = $report.source.revision
        configuration = 'Release'
        datasetId     = $datasetId
        runs          = [int]$Runs
        metrics       = $recorded
    }
    ($baseline | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $baselinePath -Encoding ascii
    Write-Host "Recorded baseline for $machine in $baselinePath ($($recorded.Count) metrics)."
    exit 0
}

if (-not $baseline.machines.Contains($machine)) {
    Write-Host "No local baseline for '$machine'. Record one with: tools\CheckPerf.ps1 -UpdateBaseline"
    exit 0
}

$entry = $baseline.machines[$machine]
$regressed = 0
Write-Host ("Baseline for {0} recorded {1} at {2}; tolerance {3}%" -f $machine, $entry.recordedUtc, $entry.revision, $TolerancePercent)
Write-Host ("{0,-52} {1,12} {2,12} {3,9}  {4}" -f 'metric', 'baseline', 'current', 'delta', 'verdict')
foreach ($m in ($metrics | Sort-Object id)) {
    $base = $entry.metrics.PSObject.Properties[$m.id]
    if (-not $base) { Write-Host ("{0,-52} not in baseline" -f $m.id); continue }
    $b = [double]$base.Value.median
    $c = [double]$m.median
    $deltaPct = if ($b -ne 0) { ($c - $b) / $b * 100 } else { 0 }
    $worse = if ($m.direction -eq 'lower') { $deltaPct } else { -$deltaPct }
    $absDiff = [Math]::Abs($c - $b)
    $verdict = 'ok'
    if ($worse -gt $TolerancePercent -and -not ($m.unit -eq 'ms' -and $absDiff -lt $MillisecondNoiseFloor)) {
        $verdict = 'REGRESSED'
        $regressed++
    }
    elseif ($worse -lt -$TolerancePercent) { $verdict = 'improved' }
    Write-Host ("{0,-52} {1,12:N2} {2,12:N2} {3,8:N1}%  {4}" -f $m.id, $b, $c, $deltaPct, $verdict)
}
if ($regressed -gt 0) {
    Write-Host "$regressed metric(s) regressed beyond tolerance (advisory; re-run to rule out noise)." -ForegroundColor Yellow
    if ($Strict) { exit 1 }
}
else { Write-Host 'No metric regressed beyond tolerance.' -ForegroundColor Green }
exit 0
