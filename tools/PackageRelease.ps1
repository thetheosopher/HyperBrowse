[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$BuildDir = ''
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

function Get-ProjectVersion {
    param([string]$CMakeListsPath)

    $match = Select-String -Path $CMakeListsPath -Pattern 'project\(HyperBrowse VERSION ([0-9]+\.[0-9]+\.[0-9]+)' | Select-Object -First 1
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

$projectRoot = (Resolve-Path $ProjectRoot).Path
$buildDir = (Resolve-Path $BuildDir).Path
$version = Get-ProjectVersion -CMakeListsPath (Join-Path $projectRoot 'CMakeLists.txt')

$distDir = Join-Path $buildDir 'dist'
$portableDir = Join-Path $distDir "HyperBrowse-$version-portable"
$runtimeDir = Join-Path $distDir "HyperBrowse-$version-installer-layout"
$portableZip = Join-Path $distDir "HyperBrowse-$version-portable-win64.zip"
$runtimeZip = Join-Path $distDir "HyperBrowse-$version-installer-layout.zip"
$installerExe = Join-Path $distDir "HyperBrowse-$version-installer.exe"
$iexpressDir = Join-Path $distDir 'iexpress'
$installerPayloadZipName = Split-Path -Leaf $runtimeZip
$installPs1Path = Join-Path $iexpressDir 'install-release.ps1'
$installCmdPath = Join-Path $iexpressDir 'install-release.cmd'
$sedPath = Join-Path $iexpressDir 'package.sed'

Write-Host "Packaging HyperBrowse $version from $projectRoot" -ForegroundColor Green

foreach ($path in @($portableDir, $runtimeDir, $iexpressDir)) {
    if (Test-Path $path) {
        Remove-Item -Path $path -Recurse -Force
    }
}

foreach ($file in @($portableZip, $runtimeZip, $installerExe)) {
    if (Test-Path $file) {
        Remove-Item -Path $file -Force
    }
}

New-Item -ItemType Directory -Path $distDir -Force | Out-Null
New-Item -ItemType Directory -Path $iexpressDir -Force | Out-Null

Invoke-External -Description 'Build Release preset' -FilePath 'cmake' -ArgumentList @('--build', '--preset', 'release')
Invoke-External -Description 'Run Release smoke tests' -FilePath 'ctest' -ArgumentList @('--preset', 'release-tests')

Invoke-External -Description 'Stage portable release layout' -FilePath 'cmake' -ArgumentList @(
    '--install', $buildDir,
    '--config', 'Release',
    '--component', 'Portable',
    '--prefix', $portableDir)

Invoke-External -Description 'Stage installer release layout' -FilePath 'cmake' -ArgumentList @(
    '--install', $buildDir,
    '--config', 'Release',
    '--component', 'Runtime',
    '--prefix', $runtimeDir)

Write-Host '==> Create release archives' -ForegroundColor Cyan
Compress-Archive -Path $portableDir -DestinationPath $portableZip -CompressionLevel Optimal -Force
Compress-Archive -Path (Join-Path $runtimeDir '*') -DestinationPath $runtimeZip -CompressionLevel Optimal -Force

Copy-Item -Path $runtimeZip -Destination (Join-Path $iexpressDir $installerPayloadZipName) -Force

$installPs1 = @"
param()

`$ErrorActionPreference = 'Stop'
`$scriptRoot = Split-Path -Parent `$MyInvocation.MyCommand.Path
`$payloadZip = Join-Path `$scriptRoot '$installerPayloadZipName'
`$installRoot = Join-Path `$env:LOCALAPPDATA 'Programs\HyperBrowse'
`$startMenuFolder = Join-Path `$env:APPDATA 'Microsoft\Windows\Start Menu\Programs\HyperBrowse'
`$extractRoot = Join-Path `$env:TEMP ('HyperBrowse-Install-' + [guid]::NewGuid().ToString('N'))

New-Item -ItemType Directory -Path `$extractRoot -Force | Out-Null

try {
    if (Test-Path `$installRoot) {
        Remove-Item -Path `$installRoot -Recurse -Force
    }

    Expand-Archive -Path `$payloadZip -DestinationPath `$extractRoot -Force

    New-Item -ItemType Directory -Path `$installRoot -Force | Out-Null
    Copy-Item -Path (Join-Path `$extractRoot '*') -Destination `$installRoot -Recurse -Force

    `$exePath = Join-Path `$installRoot 'bin\HyperBrowse.exe'
    New-Item -ItemType Directory -Path `$startMenuFolder -Force | Out-Null

    `$shell = New-Object -ComObject WScript.Shell
    `$shortcut = `$shell.CreateShortcut((Join-Path `$startMenuFolder 'HyperBrowse.lnk'))
    `$shortcut.TargetPath = `$exePath
    `$shortcut.WorkingDirectory = Split-Path -Parent `$exePath
    `$shortcut.IconLocation = "`$exePath,0"
    `$shortcut.Save()

    Start-Process -FilePath `$exePath
}
finally {
    Remove-Item -Path `$extractRoot -Recurse -Force -ErrorAction SilentlyContinue
}
"@

$installCmd = "@echo off`r`npowershell.exe -ExecutionPolicy Bypass -NoProfile -File `"%~dp0install-release.ps1`"`r`n"

$sed = @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=1
HideExtractAnimation=0
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=
DisplayLicense=
FinishMessage=HyperBrowse installation completed.
TargetName=$installerExe
FriendlyName=HyperBrowse $version Installer
AppLaunched=install-release.cmd
PostInstallCmd=<None>
AdminQuietInstCmd=install-release.cmd
UserQuietInstCmd=install-release.cmd
SourceFiles=SourceFiles
FILE0=install-release.cmd
FILE1=install-release.ps1
FILE2=$installerPayloadZipName
[Strings]
InstallPrompt=
DisplayLicense=
FinishMessage=HyperBrowse installation completed.
TargetName=$installerExe
FriendlyName=HyperBrowse $version Installer
AppLaunched=install-release.cmd
PostInstallCmd=<None>
AdminQuietInstCmd=install-release.cmd
UserQuietInstCmd=install-release.cmd
FILE0=install-release.cmd
FILE1=install-release.ps1
FILE2=$installerPayloadZipName
[SourceFiles]
SourceFiles0=$iexpressDir\
[SourceFiles0]
%FILE0%=
%FILE1%=
%FILE2%=
"@

Set-Content -Path $installPs1Path -Value $installPs1 -Encoding Ascii
Set-Content -Path $installCmdPath -Value $installCmd -Encoding Ascii
Set-Content -Path $sedPath -Value $sed -Encoding Ascii

Write-Host '==> Create self-extracting installer with IExpress' -ForegroundColor Cyan
$iexpressProcess = Start-Process -FilePath 'iexpress.exe' -ArgumentList @('/N', '/Q', $sedPath) -Wait -PassThru
if ($iexpressProcess.ExitCode -ne 0) {
    throw "Create self-extracting installer with IExpress failed with exit code $($iexpressProcess.ExitCode)."
}

if (-not (Test-Path $installerExe)) {
    throw "Expected installer was not created: $installerExe"
}

Write-Host ''
Write-Host 'Release artifacts created:' -ForegroundColor Green
Write-Host "  Portable layout:   $portableDir"
Write-Host "  Portable zip:      $portableZip"
Write-Host "  Installer layout:  $runtimeDir"
Write-Host "  Installer zip:     $runtimeZip"
Write-Host "  Installer exe:     $installerExe"