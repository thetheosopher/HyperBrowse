[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$BuildDir = '',
    [string]$Configuration = 'Release',
    [string]$ExecutablePath = '',
    [string]$DatasetPath = '',
    [string]$OutputPath = '',
    [string]$RegistryPath = 'HKCU:\Software\HyperBrowse\StartupBenchmark',
    [string]$LogPath = '',
    [int]$StartupTimeoutSeconds = 20,
    [int]$ShutdownTimeoutSeconds = 15,
    [double]$ProcessToFirstWindowVisibleBudgetMs = 0,
    [double]$FirstWindowVisibleToFirstThumbnailPaintedBudgetMs = 0,
    [double]$ProcessToFirstThumbnailPaintedBudgetMs = 0
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

$ProjectRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $ExecutablePath = Join-Path (Join-Path $BuildDir $Configuration) 'HyperBrowse.exe'
}
if ([string]::IsNullOrWhiteSpace($DatasetPath)) {
    $DatasetPath = Join-Path $ProjectRoot 'assets'
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $BuildDir ("startup-benchmark-" + $Configuration.ToLowerInvariant() + ".json")
}
if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = Join-Path $env:TEMP 'HyperBrowse-debug.log'
}

$ExecutablePath = [System.IO.Path]::GetFullPath($ExecutablePath)
$DatasetPath = [System.IO.Path]::GetFullPath($DatasetPath)
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
$logDirectory = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($logDirectory)) {
    New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
}

if (-not (Test-Path $ExecutablePath)) {
    throw "HyperBrowse executable was not found: $ExecutablePath"
}
if (-not (Test-Path $DatasetPath)) {
    throw "Startup benchmark dataset folder was not found: $DatasetPath"
}

function Assert-Budget {
    param(
        [string]$Label,
        [double]$Actual,
        [double]$Budget
    )

    if ($Budget -le 0) {
        return
    }

    if ($Actual -gt $Budget) {
        throw "$Label exceeded its budget. Actual: $Actual ms. Budget: $Budget ms."
    }
}

function Write-StepSummary {
    param(
        [double]$ProcessToWindowVisibleMs,
        [double]$WindowVisibleToFirstThumbnailMs,
        [double]$ProcessToFirstThumbnailMs,
        [string]$SnapshotPath
    )

    if ([string]::IsNullOrWhiteSpace($env:GITHUB_STEP_SUMMARY)) {
        return
    }

    @(
        '## Startup Benchmark',
        '',
        "- Process to first window visible: $ProcessToWindowVisibleMs ms",
        "- First window visible to first thumbnail painted: $WindowVisibleToFirstThumbnailMs ms",
        "- Process to first thumbnail painted: $ProcessToFirstThumbnailMs ms",
        "- Snapshot: $SnapshotPath"
    ) | Add-Content -Path $env:GITHUB_STEP_SUMMARY
}

function ConvertTo-ChildRegistrySubkey {
    param([string]$ProviderPath)

    $normalized = $ProviderPath.Trim()
    foreach ($prefix in @('HKCU:\', 'HKCU\', 'HKEY_CURRENT_USER\')) {
        if ($normalized.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            $normalized = $normalized.Substring($prefix.Length)
            break
        }
    }
    if ([string]::IsNullOrWhiteSpace($normalized) -or $normalized.Contains(':')) {
        throw "RegistryPath must identify a subkey under HKEY_CURRENT_USER: $ProviderPath"
    }
    return $normalized
}

function ConvertTo-WindowsCommandLineArgument {
    param([AllowEmptyString()][string]$Value)

    $builder = New-Object System.Text.StringBuilder
    $null = $builder.Append('"')
    $backslashCount = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') {
            $backslashCount++
            continue
        }
        if ($character -eq '"') {
            $null = $builder.Append('\', ($backslashCount * 2) + 1)
            $null = $builder.Append('"')
            $backslashCount = 0
            continue
        }
        if ($backslashCount -gt 0) {
            $null = $builder.Append('\', $backslashCount)
            $backslashCount = 0
        }
        $null = $builder.Append($character)
    }
    if ($backslashCount -gt 0) {
        $null = $builder.Append('\', $backslashCount * 2)
    }
    $null = $builder.Append('"')
    return $builder.ToString()
}

if (Get-Process HyperBrowse -ErrorAction SilentlyContinue) {
    throw 'HyperBrowse is already running; startup benchmark requires exclusive access.'
}

if (-not (Test-Path -Path $RegistryPath)) {
    New-Item -Path $RegistryPath -Force | Out-Null
}
$settingsKey = Get-Item -Path $RegistryPath
$hadPreviousSelectedFolder = $settingsKey.GetValueNames() -contains 'SelectedFolderPath'
$previousSelectedFolder = if ($hadPreviousSelectedFolder) {
    [string]$settingsKey.GetValue('SelectedFolderPath', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
} else {
    $null
}
$settingsEnvironmentName = 'HYPERBROWSE_SETTINGS_REGISTRY_PATH'
$previousSettingsEnvironment = [System.Environment]::GetEnvironmentVariable($settingsEnvironmentName, 'Process')
$childRegistrySubkey = ConvertTo-ChildRegistrySubkey -ProviderPath $RegistryPath

try {
    Set-ItemProperty -Path $RegistryPath -Name SelectedFolderPath -Value $DatasetPath
    [System.Environment]::SetEnvironmentVariable($settingsEnvironmentName, $childRegistrySubkey, 'Process')
    Remove-Item -Path $OutputPath -ErrorAction SilentlyContinue

    $beforeLogCount = if (Test-Path $LogPath) { (Get-Content -Path $LogPath).Count } else { 0 }

    $processStartInfo = New-Object System.Diagnostics.ProcessStartInfo
    $processStartInfo.FileName = $ExecutablePath
    $processStartInfo.UseShellExecute = $false
    $quotedArguments = @('--bench-startup', $OutputPath) |
        ForEach-Object { ConvertTo-WindowsCommandLineArgument -Value $_ }
    $processStartInfo.Arguments = $quotedArguments -join ' '
    $process = [System.Diagnostics.Process]::Start($processStartInfo)
    if ($null -eq $process) {
        throw "Failed to start HyperBrowse: $ExecutablePath"
    }
    try {
        $null = $process.WaitForInputIdle(15000)
    }
    catch {
    }

    if (-not $process.WaitForExit($StartupTimeoutSeconds * 1000)) {
        if (-not $process.CloseMainWindow()) {
            Stop-Process -Id $process.Id -Force
            $process.WaitForExit()
            throw "Startup benchmark could not request a normal application shutdown after $StartupTimeoutSeconds seconds."
        }
    }
    if (-not $process.WaitForExit($ShutdownTimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
        throw "Startup benchmark forced process termination after a $ShutdownTimeoutSeconds-second shutdown timeout."
    }

    if ($process.ExitCode -ne 0) {
        throw "HyperBrowse startup benchmark failed with exit code $($process.ExitCode)."
    }

    if (-not (Test-Path $OutputPath)) {
        throw "Startup benchmark output was not created: $OutputPath"
    }

    $json = Get-Content -Path $OutputPath -Raw | ConvertFrom-Json
    if (-not $json.startup.windowVisibleCaptured -or -not $json.startup.firstThumbnailPaintedCaptured) {
        throw 'Startup benchmark snapshot did not capture all required startup milestones.'
    }

    $processToWindowVisibleMs = [double]$json.startup.processToFirstWindowVisibleMs
    $windowVisibleToFirstThumbnailMs = [double]$json.startup.firstWindowVisibleToFirstThumbnailPaintedMs
    $processToFirstThumbnailMs = [double]$json.startup.processToFirstThumbnailPaintedMs

    Write-Host ('process_exit_code=' + $process.ExitCode)
    Write-Host ('benchmark_json=' + $OutputPath)
    Write-Host ('process_to_first_window_visible_ms=' + $processToWindowVisibleMs)
    Write-Host ('first_window_visible_to_first_thumbnail_painted_ms=' + $windowVisibleToFirstThumbnailMs)
    Write-Host ('process_to_first_thumbnail_painted_ms=' + $processToFirstThumbnailMs)
    Write-Host ('timing_rows=' + $json.timings.Count)
    Write-Host ('counter_rows=' + $json.counters.Count)

    Assert-Budget -Label 'process_to_first_window_visible' -Actual $processToWindowVisibleMs -Budget $ProcessToFirstWindowVisibleBudgetMs
    Assert-Budget -Label 'first_window_visible_to_first_thumbnail_painted' -Actual $windowVisibleToFirstThumbnailMs -Budget $FirstWindowVisibleToFirstThumbnailPaintedBudgetMs
    Assert-Budget -Label 'process_to_first_thumbnail_painted' -Actual $processToFirstThumbnailMs -Budget $ProcessToFirstThumbnailPaintedBudgetMs

    Write-StepSummary -ProcessToWindowVisibleMs $processToWindowVisibleMs `
                      -WindowVisibleToFirstThumbnailMs $windowVisibleToFirstThumbnailMs `
                      -ProcessToFirstThumbnailMs $processToFirstThumbnailMs `
                      -SnapshotPath $OutputPath

    if (Test-Path $LogPath) {
        $logTail = Get-Content -Path $LogPath | Select-Object -Skip $beforeLogCount
        if ($logTail) {
            Write-Host 'recent_log_lines_begin'
            $logTail | Select-Object -Last 20
            Write-Host 'recent_log_lines_end'
        }
    }
}
finally {
    [System.Environment]::SetEnvironmentVariable($settingsEnvironmentName, $previousSettingsEnvironment, 'Process')
    if ($hadPreviousSelectedFolder) {
        New-Item -Path $RegistryPath -Force | Out-Null
        New-ItemProperty -Path $RegistryPath -Name SelectedFolderPath -Value $previousSelectedFolder -PropertyType String -Force | Out-Null
    }
    else {
        Remove-ItemProperty -Path $RegistryPath -Name SelectedFolderPath -ErrorAction SilentlyContinue
    }
}
