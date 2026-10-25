<#
.SYNOPSIS
Local, no-cloud quality gate for HyperBrowse.

.DESCRIPTION
Default: configure, build Debug (HyperBrowse + HyperBrowseTests) and run the full CTest suite.
  -Quick   Debug build plus a focused CTest subset that does not drive the interactive desktop.
  -Release Release build and Release CTest (alone, or in addition to the default gate).
  -Asan    AddressSanitizer boundary-fuzz build and run in build-asan (mirrors the former CI job).
  -Tidy    clang-tidy (repo .clang-tidy) over changed files, or every source with -All.
  -Perf    Advisory local benchmark comparison through tools/CheckPerf.ps1.

-Asan, -Tidy and -Perf run on their own; combine them with -Quick or -Release to also run a
build/test gate. The UI smokes need an unlocked interactive desktop on a large display.
Nothing here calls a hosted service.
#>
[CmdletBinding()]
param(
    [switch]$Quick,
    [switch]$Release,
    [switch]$Asan,
    [switch]$Tidy,
    [switch]$Perf,
    [switch]$UpdatePerfBaseline,
    [string]$Filter = '',
    [string]$Since = 'HEAD',
    [switch]$All,
    [int]$TidyJobs = 0,
    [switch]$SkipDesktopCheck
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $PSCommandPath }
$root = [System.IO.Path]::GetFullPath((Split-Path -Parent $scriptRoot))
Set-Location $root

# Tests that exercise policies, caches and persistence without needing the interactive desktop.
$quickFilter = 'Policy|ThemeTokens|Thumbnail|JpegOrientation|ItemNumberNavigation'
$runBuildGate = $Quick -or $Release -or -not ($Asan -or $Tidy -or $Perf)
$results = New-Object System.Collections.Generic.List[object]

function Invoke-Step {
    param([string]$Name, [scriptblock]$Body)
    Write-Host ""
    Write-Host "=== $Name ===" -ForegroundColor Cyan
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $ok = $true
    $detail = ''
    $script:StepDetail = ''
    try {
        $global:LASTEXITCODE = 0
        & $Body | Out-Host
        $detail = $script:StepDetail
        if ($LASTEXITCODE -ne 0) { $ok = $false; $detail = "exit code $LASTEXITCODE" }
    }
    catch {
        $ok = $false
        $detail = $_.Exception.Message
    }
    $results.Add([pscustomobject]@{ Step = $Name; Ok = $ok; Seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1); Detail = $detail })
    return $ok
}

function Find-VsInstall {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'vswhere.exe was not found; install Visual Studio with the C++ workload.' }
    $path = & $vswhere -latest -prerelease -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $path) { throw 'No Visual Studio installation was found.' }
    return ($path | Select-Object -First 1)
}

function Import-VsDevEnvironment {
    param([string]$VsPath)
    $vcvars = Join-Path $VsPath 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path -LiteralPath $vcvars)) { throw "vcvars64.bat was not found: $vcvars" }
    $lines = & $env:ComSpec /c "`"$vcvars`" >nul && set"
    foreach ($line in $lines) {
        $i = $line.IndexOf('=')
        if ($i -gt 0) { Set-Item -LiteralPath ("Env:" + $line.Substring(0, $i)) -Value $line.Substring($i + 1) }
    }
}

function Assert-Desktop {
    if ($SkipDesktopCheck) { return }
    if (Get-Process -Name LogonUI -ErrorAction SilentlyContinue) {
        throw 'The session is locked (LogonUI is running). UI smokes need the unlocked interactive desktop. Use -SkipDesktopCheck to override.'
    }
    Add-Type -AssemblyName System.Windows.Forms
    $primary = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    if ($primary.Width -lt 1920 -or $primary.Height -lt 1080) {
        throw ("The primary display is {0}x{1}. Several UI smokes assume a large display (a KVM or remote session can fall back to 1024x768). Use -SkipDesktopCheck to override." -f $primary.Width, $primary.Height)
    }
}

function Invoke-BuildGate {
    param([string]$Config)   # Debug | Release
    $lower = $Config.ToLowerInvariant()
    $okConfigure = Invoke-Step "Configure ($Config)" { cmake --preset vs2026-x64 }
    if (-not $okConfigure) { return }
    $okBuild = Invoke-Step "Build $Config (HyperBrowse, HyperBrowseTests)" {
        cmake --build --preset $lower --target HyperBrowse HyperBrowseTests
    }
    if (-not $okBuild) { return }
    $isQuick = $Quick -and $Config -eq 'Debug'
    if (-not $isQuick) {
        $okDesktop = Invoke-Step 'Desktop preflight' { Assert-Desktop }
        if (-not $okDesktop) { return }
    }
    $pattern = if ($Filter) { $Filter } elseif ($isQuick) { $quickFilter } else { '' }
    $label = if ($pattern) { "CTest $Config (-R $pattern)" } else { "CTest $Config (all)" }
    Invoke-Step $label {
        if ($pattern) { ctest --preset "$lower-tests" -R $pattern --no-tests=error } else { ctest --preset "$lower-tests" }
    } | Out-Null
}

function Invoke-AsanGate {
    $buildDir = Join-Path $root 'build-asan'
    $ok = Invoke-Step 'ASan configure' {
        cmake -S . -B $buildDir -G 'Visual Studio 18 2026' -A x64 `
            -DHYPERBROWSE_BUILD_TESTS=ON -DHYPERBROWSE_BUILD_FUZZ_TESTS=ON `
            -DHYPERBROWSE_ENABLE_LIBRAW=OFF -DHYPERBROWSE_ENABLE_NVJPEG=OFF `
            -DHYPERBROWSE_BUNDLE_CUDA_REDIST=OFF -DHYPERBROWSE_STATIC_MSVC_RUNTIME=OFF `
            -DHYPERBROWSE_WARNINGS_AS_ERRORS=ON `
            '-DCMAKE_CXX_FLAGS=/fsanitize=address' '-DCMAKE_EXE_LINKER_FLAGS_DEBUG=/DEBUG /INCREMENTAL:NO'
    }
    if (-not $ok) { return }
    $ok = Invoke-Step 'ASan build (HyperBrowseBoundaryFuzz)' {
        cmake --build $buildDir --config Debug --target HyperBrowseBoundaryFuzz -- /m /v:minimal
    }
    if (-not $ok) { return }
    Invoke-Step 'ASan boundary fuzz tests' {
        $cache = Join-Path $buildDir 'CMakeCache.txt'
        $instance = (Select-String -Path $cache -Pattern '^CMAKE_GENERATOR_INSTANCE:INTERNAL=(.+)$' | Select-Object -First 1).Matches[0].Groups[1].Value
        $versionLine = Select-String -Path $cache -Pattern '^CMAKE_VS_PLATFORM_TOOLSET_VERSION:[^=]*=(.+)$' | Select-Object -First 1
        $toolset = if ($versionLine) { $versionLine.Matches[0].Groups[1].Value } else { (Get-Content -LiteralPath (Join-Path $instance 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt') -Raw).Trim() }
        $runtimeDir = Join-Path $instance "VC\Tools\MSVC\$toolset\bin\Hostx64\x64"
        if (-not (Test-Path -LiteralPath (Join-Path $runtimeDir 'clang_rt.asan_dynamic-x86_64.dll'))) {
            throw "The MSVC AddressSanitizer runtime was not found for toolset $toolset."
        }
        $env:PATH = "$runtimeDir;$env:PATH"
        $env:ASAN_OPTIONS = 'abort_on_error=1:halt_on_error=1:detect_leaks=0'
        $log = Join-Path $buildDir 'asan-fuzz.log'
        ctest --test-dir $buildDir -C Debug -R 'HyperBrowse(PersistentCache|RawHelperProtocol)Fuzz' --no-tests=error --output-on-failure 2>&1 | Tee-Object -FilePath $log
    } | Out-Null
}

function Get-TidyTargets {
    $db = Get-Content -LiteralPath (Join-Path $root 'build-tidy\compile_commands.json') -Raw | ConvertFrom-Json
    $known = @{}
    foreach ($entry in $db) {
        $full = [System.IO.Path]::GetFullPath($entry.file)
        if ($full.StartsWith((Join-Path $root 'src'), [System.StringComparison]::OrdinalIgnoreCase) -or
            $full.StartsWith((Join-Path $root 'tests'), [System.StringComparison]::OrdinalIgnoreCase)) {
            $known[$full.ToLowerInvariant()] = $full
        }
    }
    if ($All) { return @($known.Values | Sort-Object) }
    $changed = @(git diff --name-only --diff-filter=d $Since) + @(git ls-files --others --exclude-standard)
    $picked = foreach ($rel in ($changed | Where-Object { $_ -match '\.cpp$' })) {
        $full = [System.IO.Path]::GetFullPath((Join-Path $root $rel)).ToLowerInvariant()
        if ($known.ContainsKey($full)) { $known[$full] }
    }
    return @($picked | Sort-Object -Unique)
}

function Invoke-TidyGate {
    $vs = Find-VsInstall
    $tidyExe = Join-Path $vs 'VC\Tools\Llvm\x64\bin\clang-tidy.exe'
    if (-not (Test-Path -LiteralPath $tidyExe)) {
        $cmd = Get-Command clang-tidy -ErrorAction SilentlyContinue
        if (-not $cmd) { throw 'clang-tidy was not found (install the "C++ Clang tools for Windows" component).' }
        $tidyExe = $cmd.Source
    }
    $ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
    if (-not (Test-Path -LiteralPath $ninja)) { throw "Ninja was not found: $ninja" }
    Import-VsDevEnvironment $vs

    $ok = Invoke-Step 'Tidy: generate compile database (build-tidy, configure only)' {
        cmake -S . -B build-tidy -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" -DCMAKE_BUILD_TYPE=Debug `
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DHYPERBROWSE_BUILD_TESTS=ON -DHYPERBROWSE_BUNDLE_CUDA_REDIST=OFF
    }
    if (-not $ok) { return }

    Invoke-Step 'Tidy: clang-tidy' {
        $targets = Get-TidyTargets
        if ($targets.Count -eq 0) {
            $script:StepDetail = 'no changed .cpp files (header-only changes are not checked; use -All)'
            return
        }
        $logDir = Join-Path $root 'build-tidy\logs'
        if (Test-Path -LiteralPath $logDir) { Remove-Item -LiteralPath $logDir -Recurse -Force }
        New-Item -ItemType Directory -Path $logDir | Out-Null
        $jobs = if ($TidyJobs -gt 0) { $TidyJobs } else { [Math]::Max(1, [Environment]::ProcessorCount - 2) }
        Write-Host ("Running clang-tidy on {0} file(s), {1} at a time" -f $targets.Count, $jobs)

        $running = @()
        $n = 0
        foreach ($file in $targets) {
            while (@($running | Where-Object { -not $_.HasExited }).Count -ge $jobs) { Start-Sleep -Milliseconds 200 }
            $n++
            $log = Join-Path $logDir ("{0:D3}-{1}.log" -f $n, [System.IO.Path]::GetFileName($file))
            $running += Start-Process -FilePath $tidyExe -ArgumentList @('-p', 'build-tidy', '--quiet', "`"$file`"") `
                -NoNewWindow -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
        }
        $running | ForEach-Object { $_.WaitForExit() }

        $pattern = '^(?<loc>.+?:\d+:\d+): (?<sev>warning|error): .*\[(?<check>[\w.,-]+)\]$'
        $seen = @{}
        $byCheck = @{}
        $byFile = @{}
        foreach ($log in Get-ChildItem -LiteralPath $logDir -Filter '*.log') {
            foreach ($line in Get-Content -LiteralPath $log.FullName) {
                $m = [regex]::Match($line, $pattern)
                if (-not $m.Success -or $seen.ContainsKey($line)) { continue }
                $seen[$line] = $true
                $check = $m.Groups['check'].Value
                $loc = $m.Groups['loc'].Value
                $file = ($loc -replace ':\d+:\d+$', '').Replace($root + '\', '')
                $byCheck[$check] = 1 + [int]$byCheck[$check]
                $byFile[$file] = 1 + [int]$byFile[$file]
            }
        }
        $summary = Join-Path $root 'build-tidy\tidy-summary.txt'
        $out = New-Object System.Collections.Generic.List[string]
        $out.Add("clang-tidy: $($targets.Count) file(s), $($seen.Count) unique finding(s)")
        $out.Add('By check:')
        $byCheck.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { $out.Add(("  {0,5}  {1}" -f $_.Value, $_.Key)) }
        $out.Add('By file (top 15):')
        $byFile.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 15 | ForEach-Object { $out.Add(("  {0,5}  {1}" -f $_.Value, $_.Key)) }
        $out | Set-Content -LiteralPath $summary
        $out | ForEach-Object { Write-Host $_ }
        Write-Host "Full logs: $logDir"
        # Findings are advisory until the recorded baseline in docs/testing.md reaches zero.
        $script:StepDetail = "$($seen.Count) finding(s); see build-tidy\tidy-summary.txt"
    } | Out-Null
}

if ($runBuildGate) {
    if ($Release) { Invoke-BuildGate 'Release' }
    if (-not $Release -or $Quick) { Invoke-BuildGate 'Debug' }
}
if ($Asan) { Invoke-AsanGate }
if ($Tidy) { Invoke-TidyGate }
if ($Perf) {
    Invoke-Step 'Local perf check (advisory)' {
        $perfArgs = @{}
        if ($UpdatePerfBaseline) { $perfArgs['UpdateBaseline'] = $true }
        & (Join-Path $scriptRoot 'CheckPerf.ps1') @perfArgs
    } | Out-Null
}

Write-Host ""
Write-Host "=== Summary ===" -ForegroundColor Cyan
$results | ForEach-Object {
    $mark = if ($_.Ok) { 'PASS' } else { 'FAIL' }
    $color = if ($_.Ok) { 'Green' } else { 'Red' }
    $detail = if ($_.Detail) { "  ($($_.Detail))" } else { '' }
    Write-Host ("[{0}] {1} - {2}s{3}" -f $mark, $_.Step, $_.Seconds, $detail) -ForegroundColor $color
}
if (@($results | Where-Object { -not $_.Ok }).Count -gt 0) { exit 1 }
exit 0
