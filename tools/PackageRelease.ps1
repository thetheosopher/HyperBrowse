[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$BuildDir = '',
    [string]$Configuration = 'Release',
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [string]$InnoSetupCompiler = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $PSCommandPath }
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Split-Path -Parent $scriptRoot
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $ProjectRoot 'build-release-package'
}

function Get-ProjectVersion {
    param([string]$CMakeListsPath)

    $match = Select-String -Path $CMakeListsPath -Pattern 'project\(HyperBrowse VERSION ([0-9]+\.[0-9]+\.[0-9]+(?:\.[0-9]+)?)' | Select-Object -First 1
    if (-not $match) {
        throw "Failed to determine HyperBrowse version from $CMakeListsPath."
    }

    return $match.Matches[0].Groups[1].Value
}

function Invoke-External {
    param(
        [string]$Description,
        [string]$FilePath,
        [string[]]$ArgumentList
    )

    Write-Host "==> $Description" -ForegroundColor Cyan
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Resolve-InnoSetupCompiler {
    param([string]$RequestedPath)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        if (-not (Test-Path $RequestedPath)) {
            throw "The requested Inno Setup compiler path does not exist: $RequestedPath"
        }

        return (Resolve-Path $RequestedPath).Path
    }

    $command = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
        $(if ($env:ProgramFiles) { Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe' }),
        $(if (${env:ProgramFiles(x86)}) { Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe' })
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw 'Failed to locate ISCC.exe. Install Inno Setup 6 or pass -InnoSetupCompiler with the full path to ISCC.exe.'
}

function Resolve-CMakeTool {
    param([string]$ToolName)

    $toolExecutable = if ($ToolName.EndsWith('.exe', [System.StringComparison]::OrdinalIgnoreCase)) {
        $ToolName
    } else {
        "$ToolName.exe"
    }

    foreach ($candidate in @($toolExecutable, $ToolName)) {
        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($command) {
            return $command.Source
        }
    }

    $cmakeCommand = Get-Command 'cmake.exe' -ErrorAction SilentlyContinue
    if (-not $cmakeCommand) {
        $cmakeCommand = Get-Command 'cmake' -ErrorAction SilentlyContinue
    }

    if ($cmakeCommand) {
        $siblingTool = Join-Path (Split-Path -Parent $cmakeCommand.Source) $toolExecutable
        if (Test-Path $siblingTool) {
            return (Resolve-Path $siblingTool).Path
        }
    }

    $candidates = @(
        $(if ($env:ProgramFiles) { Join-Path $env:ProgramFiles "CMake\bin\$toolExecutable" }),
        $(if (${env:ProgramFiles(x86)}) { Join-Path ${env:ProgramFiles(x86)} "CMake\bin\$toolExecutable" }),
        $(if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA "Programs\CMake\bin\$toolExecutable" })
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "Failed to locate $toolExecutable. Install CMake or add it to PATH."
}

function Remove-PathWithRetry {
    param(
        [string]$Path,
        [int]$MaxAttempts = 20,
        [int]$RetryDelayMilliseconds = 500
    )

    for ($attempt = 1; $attempt -le $MaxAttempts; $attempt++) {
        try {
            if (-not (Test-Path $Path)) {
                return
            }

            Remove-Item -Path $Path -Recurse -Force -ErrorAction Stop
            return
        } catch {
            if (($attempt -eq $MaxAttempts) -or (-not (Test-Path $Path))) {
                throw
            }

            [System.Threading.Thread]::Sleep($RetryDelayMilliseconds)
        }
    }
}

function Assert-ReleaseLayoutManifest {
    param(
        [string]$Layout,
        [string[]]$RequiredRelativePaths,
        [string]$ComponentName
    )

    foreach ($relativePath in $RequiredRelativePaths) {
        $path = Join-Path $Layout $relativePath
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "$ComponentName layout is missing required file: $relativePath"
        }
    }

    $forbiddenExtensions = @(
        '.pdb', '.lib', '.obj', '.exp', '.ilk', '.iobj', '.ipdb', '.tlog',
        '.vcxproj', '.sln', '.slnx', '.cmake', '.log'
    )
    $forbiddenFiles = Get-ChildItem -LiteralPath $Layout -File -Recurse | Where-Object {
        $forbiddenExtensions -contains $_.Extension.ToLowerInvariant()
    }
    if ($forbiddenFiles) {
        $names = ($forbiddenFiles | ForEach-Object { $_.FullName }) -join ', '
        throw "$ComponentName layout contains build artifacts: $names"
    }

    $allowedRuntimeDllPattern = '^(cudart64_12|nvjpeg64_12|msvcp[0-9]+|vcruntime[0-9]+|concrt[0-9]+|ucrtbase|api-ms-win-crt-[^ ]+)\.dll$'
    $unexpectedDlls = Get-ChildItem -LiteralPath $Layout -File -Recurse | Where-Object {
        $_.Extension -ieq '.dll' -and $_.Name -notmatch $allowedRuntimeDllPattern
    }
    if ($unexpectedDlls) {
        $names = ($unexpectedDlls | ForEach-Object { $_.FullName }) -join ', '
        throw "$ComponentName layout contains unexpected DLLs: $names"
    }
}

$projectRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
$buildDir = [System.IO.Path]::GetFullPath($BuildDir)
$cmakeListsPath = Join-Path $projectRoot 'CMakeLists.txt'

if (-not (Test-Path $cmakeListsPath)) {
    throw "Failed to locate CMakeLists.txt under $projectRoot."
}

if (-not (Test-Path $buildDir)) {
    throw "Build directory does not exist: $buildDir. Run 'cmake --preset vs2026-x64-release-package' first, or pass -BuildDir to an existing configured tree."
}

$projectRoot = (Resolve-Path $projectRoot).Path
$buildDir = (Resolve-Path $buildDir).Path
$cmakeExecutable = Resolve-CMakeTool -ToolName 'cmake'
$ctestExecutable = if ($SkipTests) { $null } else { Resolve-CMakeTool -ToolName 'ctest' }
$innoSetupCompiler = Resolve-InnoSetupCompiler -RequestedPath $InnoSetupCompiler
$version = Get-ProjectVersion -CMakeListsPath (Join-Path $projectRoot 'CMakeLists.txt')
$installerScript = Join-Path $buildDir 'HyperBrowseInstaller.iss'

if (-not (Test-Path $installerScript)) {
    throw "Expected generated Inno Setup script was not found: $installerScript. Re-run CMake configure for this build tree before packaging."
}

$distDir = Join-Path $buildDir 'dist'
$portableDir = Join-Path $distDir "HyperBrowse-$version-portable"
$runtimeDir = Join-Path $distDir "HyperBrowse-$version-installer-layout"
$portableZip = Join-Path $distDir "HyperBrowse-$version-portable-win64.zip"
$installerExe = Join-Path $distDir "HyperBrowse-$version-installer.exe"

Write-Host "Packaging HyperBrowse $version from $projectRoot" -ForegroundColor Green

foreach ($path in @($portableDir, $runtimeDir)) {
    if (Test-Path $path) {
        Remove-PathWithRetry -Path $path
    }
}

foreach ($file in @($portableZip, $installerExe)) {
    if (Test-Path $file) {
        Remove-PathWithRetry -Path $file
    }
}

New-Item -ItemType Directory -Path $distDir -Force | Out-Null

if (-not $SkipBuild) {
    Invoke-External -Description "Build $Configuration application" -FilePath $cmakeExecutable -ArgumentList @(
        '--build', $buildDir,
        '--config', $Configuration,
        '--target', 'HyperBrowse')

    if (-not $SkipTests) {
        Invoke-External -Description "Build $Configuration smoke tests" -FilePath $cmakeExecutable -ArgumentList @(
            '--build', $buildDir,
            '--config', $Configuration,
            '--target', 'HyperBrowseTests')
    }
}

if (-not $SkipTests) {
    Invoke-External -Description "Run $Configuration smoke tests" -FilePath $ctestExecutable -ArgumentList @(
        '--test-dir', $buildDir,
        '-C', $Configuration,
        '--output-on-failure')
}

Invoke-External -Description 'Stage portable release layout' -FilePath $cmakeExecutable -ArgumentList @(
    '--install', $buildDir,
    '--config', $Configuration,
    '--component', 'Portable',
    '--prefix', $portableDir)

Invoke-External -Description 'Stage installer release layout' -FilePath $cmakeExecutable -ArgumentList @(
    '--install', $buildDir,
    '--config', $Configuration,
    '--component', 'Runtime',
    '--prefix', $runtimeDir)

foreach ($layout in @($portableDir, $runtimeDir)) {
    foreach ($fileName in @('user-guide.html', 'MainWindow.PNG')) {
        $helpAssetPath = Join-Path (Join-Path $layout 'docs') $fileName
        if (-not (Test-Path -LiteralPath $helpAssetPath -PathType Leaf)) {
            throw "Expected user guide asset was not staged: $helpAssetPath"
        }
    }
}

$portableManifest = @(
    'HyperBrowse.exe',
    'README.txt',
    'RUNTIME-DEPENDENCIES.txt',
    'docs\user-guide.html',
    'docs\MainWindow.PNG'
)
$runtimeManifest = @(
    'bin\HyperBrowse.exe',
    'docs\README-portable.txt',
    'docs\runtime-dependencies.txt',
    'docs\user-guide.html',
    'docs\MainWindow.PNG'
)
$portableRawHelper = Join-Path $portableDir 'HyperBrowseRawHelper.exe'
$runtimeRawHelper = Join-Path $runtimeDir 'bin\HyperBrowseRawHelper.exe'
if (Test-Path -LiteralPath $portableRawHelper -PathType Leaf) {
    $portableManifest += 'HyperBrowseRawHelper.exe'
    if (-not (Test-Path -LiteralPath $runtimeRawHelper -PathType Leaf)) {
        throw 'Runtime layout is missing HyperBrowseRawHelper.exe while the portable layout contains it.'
    }
    $runtimeManifest += 'bin\HyperBrowseRawHelper.exe'
}
Assert-ReleaseLayoutManifest -Layout $portableDir -RequiredRelativePaths $portableManifest -ComponentName 'Portable'
Assert-ReleaseLayoutManifest -Layout $runtimeDir -RequiredRelativePaths $runtimeManifest -ComponentName 'Runtime'

Write-Host '==> Create portable release archive' -ForegroundColor Cyan
Compress-Archive -Path $portableDir -DestinationPath $portableZip -CompressionLevel Optimal -Force

Invoke-External -Description 'Compile Inno Setup installer' -FilePath $innoSetupCompiler -ArgumentList @(
    '/Qp',
    "/DReleaseLayout=$runtimeDir",
    "/DOutputDir=$distDir",
    $installerScript)

if (-not (Test-Path $installerExe)) {
    throw "Expected installer was not created: $installerExe"
}

Write-Host ''
Write-Host 'Release artifacts created:' -ForegroundColor Green
Write-Host "  Portable layout:   $portableDir"
Write-Host "  Portable zip:      $portableZip"
Write-Host "  Installer layout:  $runtimeDir"
Write-Host "  Installer exe:     $installerExe"