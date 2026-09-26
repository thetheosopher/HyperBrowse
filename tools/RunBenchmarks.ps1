[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$BuildDir = '',
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [ValidateSet('All', 'Startup', 'PersistentCache')]
    [string]$Scenario = 'All',
    [ValidateSet('A', 'B', 'C', 'D', 'E')]
    [string]$DatasetId = 'A',
    [ValidateSet('process-cold', 'repeat-process')]
    [string]$CacheState = 'process-cold',
    [ValidateSet('nvjpeg', 'wic-fallback', 'unknown')]
    [string]$DecodePath = 'unknown',
    [string]$RunnerImage = '',
    [string]$ProfileId = '',
    [int]$Runs = 5,
    [int]$StartupTimeoutSeconds = 5,
    [string]$OutputRoot = '',
    [string]$MarkdownPath = '',
    [string]$BaselinePath = '',
    [switch]$CompareBaseline,
    [switch]$AllowProvisionalBaseline,
    [double]$ProcessToFirstWindowVisibleBudgetMs = 2500,
    [double]$FirstWindowVisibleToFirstThumbnailPaintedBudgetMs = 2500,
    [double]$ProcessToFirstThumbnailPaintedBudgetMs = 5000,
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
$ProjectRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $ProjectRoot 'build'
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
if ($Runs -lt 1) {
    throw 'Runs must be at least 1.'
}

if ([string]::IsNullOrWhiteSpace($RunnerImage)) {
    $RunnerImage = if ($env:GITHUB_ACTIONS -eq 'true') {
        'windows-2022'
    }
    elseif (-not [string]::IsNullOrWhiteSpace($env:ImageOS)) {
        [string]$env:ImageOS
    }
    else {
        'local-workstation'
    }
}
if ([string]::IsNullOrWhiteSpace($ProfileId)) {
    $ProfileId = "$RunnerImage/$DecodePath"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $ProjectRoot 'build/bench'
}
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
if ([string]::IsNullOrWhiteSpace($MarkdownPath)) {
    $MarkdownPath = Join-Path $ProjectRoot 'docs/perf/latest.md'
}
$MarkdownPath = [System.IO.Path]::GetFullPath($MarkdownPath)
if ([string]::IsNullOrWhiteSpace($BaselinePath)) {
    $BaselinePath = Join-Path $ProjectRoot 'docs/perf/baseline.json'
}
$BaselinePath = [System.IO.Path]::GetFullPath($BaselinePath)

$startupScript = Join-Path $ProjectRoot 'tools/TestStartupBenchmark.ps1'
$cacheScript = Join-Path $ProjectRoot 'tools/TestPersistentCacheBenchmark.ps1'
$datasetGenerator = Join-Path $ProjectRoot 'tools/GenerateBenchmarkDatasets.ps1'
$datasetDefinitionPath = Join-Path $ProjectRoot 'tests/benchmark-datasets/manifest.json'
$datasetOutputRoot = Join-Path $ProjectRoot 'build/benchmark-datasets'
$applicationPath = Join-Path (Join-Path $BuildDir $Configuration) 'HyperBrowse.exe'
$performanceExecutablePath = Join-Path (Join-Path $BuildDir 'tests') (Join-Path $Configuration 'HyperBrowsePerformanceBenchmark.exe')

$scenarioNames = switch ($Scenario) {
    'Startup' { @('Startup') }
    'PersistentCache' { @('PersistentCache') }
    default { @('Startup', 'PersistentCache') }
}
if ($scenarioNames -contains 'Startup') {
    if (-not (Test-Path -LiteralPath $applicationPath -PathType Leaf)) {
        throw "HyperBrowse executable was not found: $applicationPath"
    }
    if (-not (Test-Path -LiteralPath $datasetGenerator -PathType Leaf)) {
        throw "Benchmark dataset generator was not found: $datasetGenerator"
    }
    & $datasetGenerator -ProjectRoot $ProjectRoot -OutputRoot $datasetOutputRoot -DatasetId $DatasetId | Out-Null
    & $datasetGenerator -ProjectRoot $ProjectRoot -OutputRoot $datasetOutputRoot -DatasetId $DatasetId -VerifyOnly | Out-Null
}
if ($scenarioNames -contains 'PersistentCache' -and -not (Test-Path -LiteralPath $performanceExecutablePath -PathType Leaf)) {
    throw "Persistent-cache benchmark executable was not found: $performanceExecutablePath"
}
if (-not (Test-Path -LiteralPath $datasetDefinitionPath -PathType Leaf)) {
    throw "Benchmark dataset manifest was not found: $datasetDefinitionPath"
}

$datasetDefinitionHash = (Get-FileHash -LiteralPath $datasetDefinitionPath -Algorithm SHA256).Hash.ToLowerInvariant()
$datasets = [System.Collections.Generic.List[object]]::new()
$startupDatasetMetadata = $null
if ($scenarioNames -contains 'Startup') {
    $startupDatasetPath = Join-Path $datasetOutputRoot $DatasetId
    $startupDatasetMetadataPath = Join-Path $startupDatasetPath 'dataset.json'
    $startupDatasetMetadata = Get-Content -LiteralPath $startupDatasetMetadataPath -Raw | ConvertFrom-Json
    if ([string]$startupDatasetMetadata.status -ne 'available') {
        throw "Startup dataset $DatasetId is unavailable and cannot be benchmarked."
    }
    $datasets.Add([ordered]@{
        id = $DatasetId
        name = [string]$startupDatasetMetadata.name
        status = [string]$startupDatasetMetadata.status
        sourceManifestSha256 = [string]$startupDatasetMetadata.sourceManifestSha256
        inventorySha256 = (Get-FileHash -LiteralPath $startupDatasetMetadataPath -Algorithm SHA256).Hash.ToLowerInvariant()
        inventoryPath = $startupDatasetMetadataPath
        fileCount = [int]$startupDatasetMetadata.fileCount
        byteLength = [long]$startupDatasetMetadata.byteLength
        inventory = $startupDatasetMetadata
    })
}
if ($scenarioNames -contains 'PersistentCache') {
    $cacheWorkload = Get-Content -LiteralPath $datasetDefinitionPath -Raw | ConvertFrom-Json
    $cacheDefinition = @($cacheWorkload.syntheticBenchmarks | Where-Object { $_.id -eq 'cache-micro-v1' }) | Select-Object -First 1
    if ($null -eq $cacheDefinition) {
        throw 'The cache-micro-v1 workload is missing from the benchmark manifest.'
    }
    $datasets.Add([ordered]@{
        id = 'cache-micro-v1'
        name = 'synthetic-cache-workload'
        status = 'available'
        sourceManifestSha256 = $datasetDefinitionHash
        inventorySha256 = $datasetDefinitionHash
        inventoryPath = $datasetDefinitionPath
        fileCount = $null
        byteLength = $null
        inventory = $cacheDefinition
    })
}

$revision = (& git -C $ProjectRoot rev-parse HEAD 2>$null | Out-String).Trim()
if ([string]::IsNullOrWhiteSpace($revision)) {
    throw 'Could not determine the source revision with git rev-parse.'
}
$branch = (& git -C $ProjectRoot branch --show-current 2>$null | Out-String).Trim()
$worktreeState = (& git -C $ProjectRoot status --porcelain 2>$null | Out-String).Trim()
$environment = [ordered]@{
    runnerImage = $RunnerImage
    windowsCaption = $null
    windowsVersion = $null
    windowsBuild = $null
    cpu = $null
    gpu = $null
    ramBytes = $null
    hasNvidiaGpu = $false
    nvjpegEnabled = $DecodePath -eq 'nvjpeg'
}
try {
    $operatingSystem = Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop
    $computerSystem = Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop
    $processor = Get-CimInstance -ClassName Win32_Processor -ErrorAction Stop | Select-Object -First 1
    $videoController = Get-CimInstance -ClassName Win32_VideoController -ErrorAction Stop | Select-Object -First 1
    $environment.windowsCaption = [string]$operatingSystem.Caption
    $environment.windowsVersion = [string]$operatingSystem.Version
    $environment.windowsBuild = [string]$operatingSystem.BuildNumber
    $environment.cpu = [string]$processor.Name
    $environment.gpu = [string]$videoController.Name
    $environment.ramBytes = [long]$computerSystem.TotalPhysicalMemory
    $environment.hasNvidiaGpu = ([string]$videoController.Name -match 'NVIDIA')
}
catch {
    $environment.environmentQueryError = $_.Exception.Message
}

$applicationInfo = $null
if ($scenarioNames -contains 'Startup') {
    $applicationFile = Get-Item -LiteralPath $applicationPath
    $applicationInfo = [ordered]@{
        path = $applicationPath
        lastWriteTimeUtc = $applicationFile.LastWriteTimeUtc.ToString('o')
        configuration = $Configuration
    }
}
$performanceExecutableInfo = $null
if ($scenarioNames -contains 'PersistentCache') {
    $performanceFile = Get-Item -LiteralPath $performanceExecutablePath
    $performanceExecutableInfo = [ordered]@{
        path = $performanceExecutablePath
        lastWriteTimeUtc = $performanceFile.LastWriteTimeUtc.ToString('o')
        configuration = $Configuration
    }
}

$profileSlug = $ProfileId -replace '[^A-Za-z0-9._-]', '-'
$timestamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$runId = "$revision-$timestamp-$profileSlug-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
$runDirectory = Join-Path (Join-Path $OutputRoot $revision) $runId
$rawDirectory = Join-Path $runDirectory 'runs'
New-Item -ItemType Directory -Path $rawDirectory -Force | Out-Null

$utf8 = [System.Text.UTF8Encoding]::new($false)
function Write-JsonFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][object]$Value
    )

    $parent = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $json = ConvertTo-Json -InputObject $Value -Depth 40
    [System.IO.File]::WriteAllText($Path, $json + [Environment]::NewLine, $utf8)
}

function Get-MetricDefinitions {
    return @(
        [pscustomobject]@{ id = 'startup.processToFirstWindowVisibleMs'; scenario = 'Startup'; datasetId = $DatasetId; sourceProperty = 'processToFirstWindowVisibleMs'; unit = 'ms'; direction = 'lower' },
        [pscustomobject]@{ id = 'startup.firstWindowVisibleToFirstThumbnailPaintedMs'; scenario = 'Startup'; datasetId = $DatasetId; sourceProperty = 'firstWindowVisibleToFirstThumbnailPaintedMs'; unit = 'ms'; direction = 'lower' },
        [pscustomobject]@{ id = 'startup.processToFirstThumbnailPaintedMs'; scenario = 'Startup'; datasetId = $DatasetId; sourceProperty = 'processToFirstThumbnailPaintedMs'; unit = 'ms'; direction = 'lower' },
        [pscustomobject]@{ id = 'cache.cacheHitAverageMs'; scenario = 'PersistentCache'; datasetId = 'cache-micro-v1'; sourceProperty = 'cacheHitAverageMs'; unit = 'ms'; direction = 'lower' },
        [pscustomobject]@{ id = 'cache.storeEntriesPerSecond'; scenario = 'PersistentCache'; datasetId = 'cache-micro-v1'; sourceProperty = 'storeEntriesPerSecond'; unit = 'entries/s'; direction = 'higher' },
        [pscustomobject]@{ id = 'cache.compactionMs'; scenario = 'PersistentCache'; datasetId = 'cache-micro-v1'; sourceProperty = 'compactionMs'; unit = 'ms'; direction = 'lower' },
        [pscustomobject]@{ id = 'cache.cacheWorkerQueueDelayAverageMs'; scenario = 'PersistentCache'; datasetId = 'cache-micro-v1'; sourceProperty = 'cacheWorkerQueueDelayAverageMs'; unit = 'ms'; direction = 'lower' },
        [pscustomobject]@{ id = 'cache.cacheWorkerQueueDelayMaxMs'; scenario = 'PersistentCache'; datasetId = 'cache-micro-v1'; sourceProperty = 'cacheWorkerQueueDelayMaxMs'; unit = 'ms'; direction = 'lower' },
        [pscustomobject]@{ id = 'cache.scrollMessagesPerSecond'; scenario = 'PersistentCache'; datasetId = 'cache-micro-v1'; sourceProperty = 'scrollMessagesPerSecond'; unit = 'messages/s'; direction = 'higher' }
    )
}

function Get-Percentile {
    param(
        [Parameter(Mandatory = $true)][double[]]$Values,
        [Parameter(Mandatory = $true)][double]$Percentile
    )

    $sorted = @($Values | Sort-Object)
    $index = [Math]::Max(0, [Math]::Ceiling($Percentile * $sorted.Count) - 1)
    return [double]$sorted[$index]
}

function Get-Median {
    param([Parameter(Mandatory = $true)][double[]]$Values)

    $sorted = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)
    if ($sorted.Count % 2 -eq 0) {
        return ([double]$sorted[$middle - 1] + [double]$sorted[$middle]) / 2.0
    }
    return [double]$sorted[$middle]
}

function Get-AggregateMetrics {
    param([Parameter(Mandatory = $true)][object[]]$RunRecords)

    $aggregates = [System.Collections.Generic.List[object]]::new()
    foreach ($definition in (Get-MetricDefinitions)) {
        $values = [System.Collections.Generic.List[double]]::new()
        foreach ($record in $RunRecords) {
            if ([string]$record.role -ne 'sample' -or [string]$record.status -ne 'passed') {
                continue
            }
            if ([string]$record.scenario -ne [string]$definition.scenario -or $null -eq $record.metrics) {
                continue
            }
            if ($record.metrics.Contains([string]$definition.id)) {
                $values.Add([double]$record.metrics[[string]$definition.id])
            }
        }

        $sampleValues = $values.ToArray()
        $aggregate = [ordered]@{
            id = [string]$definition.id
            scenario = [string]$definition.scenario
            datasetId = [string]$definition.datasetId
            unit = [string]$definition.unit
            direction = [string]$definition.direction
            sampleCount = $sampleValues.Count
            samples = $sampleValues
            minimum = $null
            median = $null
            p95NearestRank = $null
            maximum = $null
            mean = $null
        }
        if ($sampleValues.Count -gt 0) {
            $aggregate.minimum = [Math]::Round(($sampleValues | Measure-Object -Minimum).Minimum, 3)
            $aggregate.median = [Math]::Round((Get-Median -Values $sampleValues), 3)
            if ($sampleValues.Count -ge 5) {
                $aggregate.p95NearestRank = [Math]::Round((Get-Percentile -Values $sampleValues -Percentile 0.95), 3)
            }
            $aggregate.maximum = [Math]::Round(($sampleValues | Measure-Object -Maximum).Maximum, 3)
            $aggregate.mean = [Math]::Round(($sampleValues | Measure-Object -Average).Average, 3)
        }
        if ($scenarioNames -contains [string]$definition.scenario) {
            $aggregates.Add($aggregate)
        }
    }

    return ,$aggregates.ToArray()
}

function Resolve-Baseline {
    param(
        [Parameter(Mandatory = $true)][object[]]$Aggregates,
        [Parameter(Mandatory = $true)][object[]]$DatasetRecords
    )

    if (-not $CompareBaseline) {
        return [ordered]@{ status = 'not-requested'; profileId = $ProfileId; comparisons = @(); message = 'Baseline comparison was not requested.' }
    }
    if (-not (Test-Path -LiteralPath $BaselinePath -PathType Leaf)) {
        if ($AllowProvisionalBaseline) {
            return [ordered]@{ status = 'calibration-pending'; profileId = $ProfileId; comparisons = @(); message = "Baseline file is missing: $BaselinePath" }
        }
        return [ordered]@{ status = 'failed'; profileId = $ProfileId; comparisons = @(); message = "Baseline file is missing: $BaselinePath" }
    }

    $baselineDocument = Get-Content -LiteralPath $BaselinePath -Raw | ConvertFrom-Json
    if ([int]$baselineDocument.schemaVersion -ne 1) {
        return [ordered]@{ status = 'failed'; profileId = $ProfileId; comparisons = @(); message = 'Unsupported baseline schema version.' }
    }
    $baselineProfile = @($baselineDocument.profiles | Where-Object { [string]$_.id -eq $ProfileId }) | Select-Object -First 1
    if ($null -eq $baselineProfile) {
        if ($AllowProvisionalBaseline) {
            return [ordered]@{ status = 'calibration-pending'; profileId = $ProfileId; comparisons = @(); message = 'No matching baseline profile has been calibrated.' }
        }
        return [ordered]@{ status = 'failed'; profileId = $ProfileId; comparisons = @(); message = 'No matching baseline profile exists.' }
    }

    $workflowSampleCount = [int]$baselineProfile.workflowSampleCount
    $minimumWorkflowSampleCount = [int]$baselineDocument.calibrationPolicy.minimumIndependentWorkflowSamplesPerProfile
    $isCalibrated = [string]$baselineProfile.calibrationStatus -eq 'calibrated' -and $workflowSampleCount -ge $minimumWorkflowSampleCount
    if (-not $isCalibrated) {
        $message = "Baseline profile is provisional ($workflowSampleCount/$minimumWorkflowSampleCount independent workflow samples)."
        if ($AllowProvisionalBaseline) {
            return [ordered]@{ status = 'calibration-pending'; profileId = $ProfileId; comparisons = @(); message = $message }
        }
        return [ordered]@{ status = 'failed'; profileId = $ProfileId; comparisons = @(); message = $message }
    }

    $runnerMatches = [string]$baselineProfile.runnerImage -eq $RunnerImage
    $decodePathMatches = [string]$baselineProfile.decodePath -eq $DecodePath
    $configurationMatches = [string]$baselineProfile.configuration -eq $Configuration
    if (-not ($runnerMatches -and $decodePathMatches -and $configurationMatches)) {
        return [ordered]@{ status = 'failed'; profileId = $ProfileId; comparisons = @(); message = 'Baseline runner image, decode path, or configuration does not match this run.' }
    }

    $comparisons = [System.Collections.Generic.List[object]]::new()
    $failedComparison = $false
    foreach ($aggregate in $Aggregates) {
        if ([int]$aggregate.sampleCount -eq 0) {
            continue
        }
        $datasetRecord = @($DatasetRecords | Where-Object { [string]$_.id -eq [string]$aggregate.datasetId }) | Select-Object -First 1
        $baselineDataset = @($baselineProfile.datasets | Where-Object { [string]$_.id -eq [string]$aggregate.datasetId }) | Select-Object -First 1
        $datasetCompatible = $null -ne $datasetRecord -and $null -ne $baselineDataset
        if ($datasetCompatible) {
            $datasetCompatible = [string]$datasetRecord.inventorySha256 -eq [string]$baselineDataset.inventorySha256
        }
        if (-not $datasetCompatible) {
            $comparisons.Add([ordered]@{ id = [string]$aggregate.id; status = 'incompatible'; message = 'Dataset inventory checksum does not match the baseline.' })
            $failedComparison = $true
            continue
        }

        $baselineMetric = @($baselineProfile.metrics | Where-Object { [string]$_.id -eq [string]$aggregate.id }) | Select-Object -First 1
        if ($null -eq $baselineMetric) {
            $comparisons.Add([ordered]@{ id = [string]$aggregate.id; status = 'missing'; message = 'Metric is missing from the calibrated baseline.' })
            $failedComparison = $true
            continue
        }

        $referenceMedian = [double]$baselineMetric.median
        $allowedDelta = [Math]::Max(
            [double]$baselineMetric.absoluteTolerance,
            [Math]::Abs($referenceMedian) * ([double]$baselineMetric.maximumRegressionPercent / 100.0))
        $threshold = if ([string]$aggregate.direction -eq 'lower') {
            $referenceMedian + $allowedDelta
        }
        else {
            $referenceMedian - $allowedDelta
        }
        $passed = if ([string]$aggregate.direction -eq 'lower') {
            [double]$aggregate.median -le $threshold
        }
        else {
            [double]$aggregate.median -ge $threshold
        }
        if (-not $passed) {
            $failedComparison = $true
        }
        $comparisons.Add([ordered]@{
            id = [string]$aggregate.id
            status = if ($passed) { 'passed' } else { 'failed' }
            observedMedian = [double]$aggregate.median
            baselineMedian = $referenceMedian
            allowedDelta = [Math]::Round($allowedDelta, 3)
            threshold = [Math]::Round($threshold, 3)
            direction = [string]$aggregate.direction
        })
    }

    return [ordered]@{
        status = if ($failedComparison) { 'failed' } else { 'passed' }
        profileId = $ProfileId
        comparisons = $comparisons.ToArray()
        message = if ($failedComparison) { 'One or more metrics failed compatibility or regression checks.' } else { 'All available metrics are within their calibrated baseline limits.' }
    }
}

function Write-MarkdownSummary {
    param([Parameter(Mandatory = $true)][object]$Report)

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('# HyperBrowse Performance Benchmark')
    $lines.Add('')
    $lines.Add("- Run: ``$($Report.runId)``")
    $lines.Add("- Revision: ``$($Report.source.revision)`` (dirty: $($Report.source.dirtyWorktree))")
    $lines.Add("- Profile: ``$($Report.selection.profileId)``")
    $lines.Add("- Runner: $($Report.environment.runnerImage); $($Report.environment.windowsCaption); $($Report.environment.cpu); $($Report.environment.gpu)")
    $lines.Add("- Decode path: ``$($Report.selection.decodePath)``; configuration: ``$($Report.selection.configuration)``")
    $lines.Add("- Baseline: $($Report.baseline.status) ($($Report.baseline.message))")
    $lines.Add("- Sample runs: $($Report.runs.Count); failed samples: $($Report.failureCount)")
    $lines.Add('')
    $lines.Add('| Metric | Dataset | Samples | Min | Median | p95 | Max | Unit |')
    $lines.Add('| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |')
    foreach ($metric in $Report.aggregate.metrics) {
        $p95 = if ($null -eq $metric.p95NearestRank) { 'n/a' } else { [string]$metric.p95NearestRank }
        $lines.Add("| $($metric.id) | $($metric.datasetId) | $($metric.sampleCount) | $($metric.minimum) | $($metric.median) | $p95 | $($metric.maximum) | $($metric.unit) |")
    }
    $lines.Add('')
    $lines.Add('## Dataset and State')
    $lines.Add('')
    foreach ($dataset in $Report.datasets) {
        $lines.Add("- ``$($dataset.id)`` ($($dataset.name)): $($dataset.fileCount) files, inventory SHA-256 ``$($dataset.inventorySha256)``")
    }
    $lines.Add("- Startup process state: $($Report.selection.cacheStateDescription)")
    $lines.Add('- OS file-cache state: uncontrolled; no cold-disk claim is made.')
    $lines.Add('')
    $lines.Add('## A5/A6 Readiness')
    $lines.Add('')
    $lines.Add('The current automated suite does not measure WIC scale-path allocation pressure or paired CPU/WIC versus GPU scale cost. A5 WIC pooling and A6 GPU scaling remain gated; this report cannot establish benefit or lack of benefit.')
    $lines.Add('')
    $lines.Add("Raw samples and the complete report are retained under ``$($Report.paths.runDirectory)``.")

    New-Item -ItemType Directory -Path (Split-Path -Parent $MarkdownPath) -Force | Out-Null
    [System.IO.File]::WriteAllText($MarkdownPath, ($lines -join [Environment]::NewLine) + [Environment]::NewLine, $utf8)
}

$metricDefinitions = Get-MetricDefinitions
$runRecords = [System.Collections.Generic.List[object]]::new()
$startedUtc = [DateTime]::UtcNow.ToString('o')
$initialReport = [ordered]@{
    schemaVersion = 1
    runnerVersion = '1.0.0'
    runId = $runId
    startedUtc = $startedUtc
    finishedUtc = $null
    source = [ordered]@{
        revision = $revision
        branch = $branch
        dirtyWorktree = -not [string]::IsNullOrWhiteSpace($worktreeState)
    }
    executable = [ordered]@{
        application = $applicationInfo
        performanceBenchmark = $performanceExecutableInfo
    }
    environment = $environment
    budgets = [ordered]@{
        startupProcessToFirstWindowVisibleMs = $ProcessToFirstWindowVisibleBudgetMs
        startupFirstWindowVisibleToFirstThumbnailPaintedMs = $FirstWindowVisibleToFirstThumbnailPaintedBudgetMs
        startupProcessToFirstThumbnailPaintedMs = $ProcessToFirstThumbnailPaintedBudgetMs
        cacheHitAverageMs = $CacheHitAverageBudgetMs
        minimumStoreEntriesPerSecond = $MinimumStoreEntriesPerSecond
        compactionMs = $CompactionBudgetMs
        cacheWorkerQueueDelayMaxMs = $CacheWorkerQueueDelayMaxBudgetMs
        minimumScrollMessagesPerSecond = $MinimumScrollMessagesPerSecond
    }
    selection = [ordered]@{
        scenario = $Scenario
        runsPerScenario = $Runs
        configuration = $Configuration
        profileId = $ProfileId
        runnerImage = $RunnerImage
        decodePath = $DecodePath
        datasetId = if ($scenarioNames -contains 'Startup') { $DatasetId } else { $null }
        cacheState = $CacheState
        cacheStateDescription = if ($CacheState -eq 'repeat-process') { 'One unmeasured warm-up launch precedes measured launches; each sample is a fresh process.' } else { 'Each sample launches a fresh process.' }
        osFileCacheState = 'uncontrolled'
        startupTimeoutSeconds = $StartupTimeoutSeconds
        benchmarkManifestSha256 = $datasetDefinitionHash
    }
    datasets = $datasets.ToArray()
    runs = @()
    aggregate = [ordered]@{ metrics = @() }
    baseline = [ordered]@{ status = 'not-requested'; profileId = $ProfileId; comparisons = @(); message = 'Baseline comparison was not requested.' }
    baselineCandidate = $null
    failureCount = 0
    paths = [ordered]@{
        runDirectory = $runDirectory
        rawDirectory = $rawDirectory
        reportJson = Join-Path $runDirectory 'report.json'
        markdown = $MarkdownPath
    }
}
Write-JsonFile -Path $initialReport.paths.reportJson -Value $initialReport

function Invoke-StartupRun {
    param(
        [Parameter(Mandatory = $true)][int]$RunNumber,
        [Parameter(Mandatory = $true)][string]$Role
    )

    $kind = if ($Role -eq 'warmup') { 'warmup' } else { 'sample' }
    $runName = 'run-{0:D2}-startup-{1}' -f $RunNumber, $Role
    $sourceSnapshotPath = Join-Path $rawDirectory "$runName.snapshot.json"
    $recordPath = Join-Path $rawDirectory "$runName.json"
    $applicationLogSourcePath = Join-Path $env:TEMP 'HyperBrowse-debug.log'
    $applicationLogPath = Join-Path $rawDirectory "$runName-app.log"
    $applicationLogStartLine = if (Test-Path -LiteralPath $applicationLogSourcePath -PathType Leaf) {
        @(Get-Content -LiteralPath $applicationLogSourcePath).Count
    } else {
        0
    }
    $runnerLogPath = Join-Path $rawDirectory "$runName-runner.log"
    $registryPath = "HKCU:\Software\HyperBrowse\Benchmark\$runId\startup-$RunNumber-$Role"
    $registryKeyCreated = $false
    $runStart = [DateTime]::UtcNow.ToString('o')
    $runStatus = 'passed'
    $failureMessage = $null
    $sourceSnapshot = $null
    $metricValues = [ordered]@{}

    try {
        if (Test-Path -LiteralPath $registryPath) {
            throw "Benchmark registry identity already exists: $registryPath"
        }
        $startupParameters = @{
            ProjectRoot = $ProjectRoot
            BuildDir = $BuildDir
            Configuration = $Configuration
            ExecutablePath = $applicationPath
            DatasetPath = Join-Path $datasetOutputRoot $DatasetId
            OutputPath = $sourceSnapshotPath
            RegistryPath = $registryPath
            LogPath = $applicationLogSourcePath
            StartupTimeoutSeconds = $StartupTimeoutSeconds
            ProcessToFirstWindowVisibleBudgetMs = if ($Role -eq 'warmup') { 0 } else { $ProcessToFirstWindowVisibleBudgetMs }
            FirstWindowVisibleToFirstThumbnailPaintedBudgetMs = if ($Role -eq 'warmup') { 0 } else { $FirstWindowVisibleToFirstThumbnailPaintedBudgetMs }
            ProcessToFirstThumbnailPaintedBudgetMs = if ($Role -eq 'warmup') { 0 } else { $ProcessToFirstThumbnailPaintedBudgetMs }
        }
        $registryKeyCreated = $true
        & $startupScript @startupParameters *> $runnerLogPath
    }
    catch {
        $runStatus = 'failed'
        $failureMessage = $_.Exception.Message
    }
    finally {
        if ($registryKeyCreated -and (Test-Path -LiteralPath $registryPath)) {
            Remove-Item -LiteralPath $registryPath -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    if (Test-Path -LiteralPath $applicationLogSourcePath -PathType Leaf) {
        $applicationLogDelta = @(Get-Content -LiteralPath $applicationLogSourcePath | Select-Object -Skip $applicationLogStartLine)
        if ($applicationLogDelta.Count -gt 0) {
            [System.IO.File]::WriteAllLines(
                $applicationLogPath,
                [string[]]$applicationLogDelta,
                [System.Text.UTF8Encoding]::new($false))
        }
    }

    if (Test-Path -LiteralPath $sourceSnapshotPath -PathType Leaf) {
        try {
            $sourceSnapshot = Get-Content -LiteralPath $sourceSnapshotPath -Raw | ConvertFrom-Json
            foreach ($definition in $metricDefinitions | Where-Object { $_.scenario -eq 'Startup' }) {
                $value = $sourceSnapshot.startup.([string]$definition.sourceProperty)
                if ($null -eq $value -or [double]::IsNaN([double]$value) -or [double]::IsInfinity([double]$value)) {
                    throw "Startup metric was missing or non-finite: $($definition.id)"
                }
                $metricValues[[string]$definition.id] = [double]$value
            }
        }
        catch {
            if ($runStatus -eq 'passed') {
                $runStatus = 'failed'
                $failureMessage = $_.Exception.Message
            }
        }
    }
    elseif ($runStatus -eq 'passed') {
        $runStatus = 'failed'
        $failureMessage = 'Startup benchmark did not retain a JSON snapshot.'
    }

    $record = [ordered]@{
        schemaVersion = 1
        runNumber = $RunNumber
        role = $kind
        scenario = 'Startup'
        status = $runStatus
        startedUtc = $runStart
        finishedUtc = [DateTime]::UtcNow.ToString('o')
        datasetId = $DatasetId
        datasetInventorySha256 = ($datasets | Where-Object { $_.id -eq $DatasetId } | Select-Object -First 1).inventorySha256
        registryIdentity = $registryPath
        sourceSnapshotPath = $sourceSnapshotPath
        applicationLogPath = $applicationLogPath
        applicationLogSourcePath = $applicationLogSourcePath
        runnerLogPath = $runnerLogPath
        metrics = $metricValues
        sourceSnapshot = $sourceSnapshot
        failureMessage = $failureMessage
    }
    Write-JsonFile -Path $recordPath -Value $record
    return [pscustomobject]$record
}

function Invoke-PersistentCacheRun {
    param([Parameter(Mandatory = $true)][int]$RunNumber)

    $runName = 'run-{0:D2}-persistent-cache' -f $RunNumber
    $sourceSnapshotPath = Join-Path $rawDirectory "$runName.snapshot.json"
    $recordPath = Join-Path $rawDirectory "$runName.json"
    $logPath = Join-Path $rawDirectory "$runName.log"
    $runStart = [DateTime]::UtcNow.ToString('o')
    $runStatus = 'passed'
    $failureMessage = $null
    $sourceSnapshot = $null
    $metricValues = [ordered]@{}

    try {
        $cacheParameters = @{
            ProjectRoot = $ProjectRoot
            BuildDir = $BuildDir
            Configuration = $Configuration
            ExecutablePath = $performanceExecutablePath
            OutputPath = $sourceSnapshotPath
            CacheHitAverageBudgetMs = $CacheHitAverageBudgetMs
            MinimumStoreEntriesPerSecond = $MinimumStoreEntriesPerSecond
            CompactionBudgetMs = $CompactionBudgetMs
            CacheWorkerQueueDelayMaxBudgetMs = $CacheWorkerQueueDelayMaxBudgetMs
            MinimumScrollMessagesPerSecond = $MinimumScrollMessagesPerSecond
        }
        & $cacheScript @cacheParameters *> $logPath
    }
    catch {
        $runStatus = 'failed'
        $failureMessage = $_.Exception.Message
    }

    if (Test-Path -LiteralPath $sourceSnapshotPath -PathType Leaf) {
        try {
            $sourceSnapshot = Get-Content -LiteralPath $sourceSnapshotPath -Raw | ConvertFrom-Json
            foreach ($definition in $metricDefinitions | Where-Object { $_.scenario -eq 'PersistentCache' }) {
                $value = $sourceSnapshot.([string]$definition.sourceProperty)
                if ($null -eq $value -or [double]::IsNaN([double]$value) -or [double]::IsInfinity([double]$value)) {
                    throw "Persistent-cache metric was missing or non-finite: $($definition.id)"
                }
                $metricValues[[string]$definition.id] = [double]$value
            }
        }
        catch {
            if ($runStatus -eq 'passed') {
                $runStatus = 'failed'
                $failureMessage = $_.Exception.Message
            }
        }
    }
    elseif ($runStatus -eq 'passed') {
        $runStatus = 'failed'
        $failureMessage = 'Persistent-cache benchmark did not retain a JSON snapshot.'
    }

    $record = [ordered]@{
        schemaVersion = 1
        runNumber = $RunNumber
        role = 'sample'
        scenario = 'PersistentCache'
        status = $runStatus
        startedUtc = $runStart
        finishedUtc = [DateTime]::UtcNow.ToString('o')
        datasetId = 'cache-micro-v1'
        datasetInventorySha256 = $datasetDefinitionHash
        sourceSnapshotPath = $sourceSnapshotPath
        logPath = $logPath
        metrics = $metricValues
        sourceSnapshot = $sourceSnapshot
        failureMessage = $failureMessage
    }
    Write-JsonFile -Path $recordPath -Value $record
    return [pscustomobject]$record
}

foreach ($scenarioName in $scenarioNames) {
    if ($scenarioName -eq 'Startup') {
        if ($CacheState -eq 'repeat-process') {
            $warmupRecord = Invoke-StartupRun -RunNumber 0 -Role 'warmup'
            $runRecords.Add($warmupRecord)
            if ([string]$warmupRecord.status -ne 'passed') {
                $initialReport.failureCount = [int]$initialReport.failureCount + 1
            }
        }
        for ($runNumber = 1; $runNumber -le $Runs; $runNumber++) {
            $runRecord = Invoke-StartupRun -RunNumber $runNumber -Role 'sample'
            $runRecords.Add($runRecord)
            if ([string]$runRecord.status -ne 'passed') {
                $initialReport.failureCount = [int]$initialReport.failureCount + 1
            }
            $initialReport.runs = @($runRecords)
            $initialReport.aggregate = [ordered]@{ metrics = (Get-AggregateMetrics -RunRecords @($runRecords)) }
            Write-JsonFile -Path $initialReport.paths.reportJson -Value $initialReport
        }
    }
    else {
        for ($runNumber = 1; $runNumber -le $Runs; $runNumber++) {
            $runRecord = Invoke-PersistentCacheRun -RunNumber $runNumber
            $runRecords.Add($runRecord)
            if ([string]$runRecord.status -ne 'passed') {
                $initialReport.failureCount = [int]$initialReport.failureCount + 1
            }
            $initialReport.runs = @($runRecords)
            $initialReport.aggregate = [ordered]@{ metrics = (Get-AggregateMetrics -RunRecords @($runRecords)) }
            Write-JsonFile -Path $initialReport.paths.reportJson -Value $initialReport
        }
    }
}

$aggregates = Get-AggregateMetrics -RunRecords @($runRecords)
$baselineResult = Resolve-Baseline -Aggregates $aggregates -DatasetRecords $datasets.ToArray()
$initialReport.finishedUtc = [DateTime]::UtcNow.ToString('o')
$initialReport.runs = @($runRecords)
$initialReport.aggregate = [ordered]@{
    metricStatistic = 'median, nearest-rank p95 when sampleCount >= 5; all successful raw samples retained'
    metrics = $aggregates
}
$initialReport.baseline = $baselineResult
$initialReport.baselineCandidate = [ordered]@{
    profileId = $ProfileId
    runnerImage = $RunnerImage
    decodePath = $DecodePath
    configuration = $Configuration
    independentWorkflowSampleCount = 1
    runRepetitionsPerScenario = $Runs
    datasetInventories = @($datasets | ForEach-Object { [ordered]@{ id = $_.id; inventorySha256 = $_.inventorySha256 } })
    metrics = @($aggregates | Where-Object { $_.sampleCount -gt 0 } | ForEach-Object {
        [ordered]@{
            id = $_.id
            unit = $_.unit
            direction = $_.direction
            sampleCount = $_.sampleCount
            samples = $_.samples
            median = $_.median
            p95NearestRank = $_.p95NearestRank
        }
    })
    status = 'candidate-not-reviewed'
}
Write-JsonFile -Path $initialReport.paths.reportJson -Value $initialReport
Write-MarkdownSummary -Report $initialReport

if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_STEP_SUMMARY)) {
    Get-Content -LiteralPath $MarkdownPath | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY
}

Write-Output "benchmark_report=$($initialReport.paths.reportJson)"
Write-Output "benchmark_markdown=$MarkdownPath"
Write-Output "benchmark_run_id=$runId"
Write-Output "baseline_status=$($baselineResult.status)"
Write-Output "failed_runs=$($initialReport.failureCount)"

$baselineFailed = [string]$baselineResult.status -eq 'failed'
if ([int]$initialReport.failureCount -gt 0 -or $baselineFailed) {
    exit 1
}
