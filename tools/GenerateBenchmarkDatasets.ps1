[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$OutputRoot = '',
    [string]$RawSourceDirectory = '',
    [ValidateSet('A', 'B', 'C', 'D', 'E')]
    [string[]]$DatasetId = @('A', 'B', 'C', 'D', 'E'),
    [switch]$VerifyOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $PSCommandPath }
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Split-Path -Parent $scriptRoot
}
$ProjectRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
$manifestPath = Join-Path $ProjectRoot 'tests/benchmark-datasets/manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Benchmark dataset definition was not found: $manifestPath"
}

$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot 'build')).TrimEnd('\')
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $buildRoot 'benchmark-datasets'
}
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$buildPrefix = $buildRoot + '\'
if (-not $OutputRoot.StartsWith($buildPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must be inside the repository build directory to prevent overwriting user data: $OutputRoot"
}

if (-not [string]::IsNullOrWhiteSpace($RawSourceDirectory)) {
    $RawSourceDirectory = [System.IO.Path]::GetFullPath($RawSourceDirectory)
    if (-not (Test-Path -LiteralPath $RawSourceDirectory -PathType Container)) {
        throw "RAW source directory was not found: $RawSourceDirectory"
    }
}

try {
    Add-Type -AssemblyName System.Drawing
}
catch {
    throw "System.Drawing is required to generate Windows benchmark fixtures: $($_.Exception.Message)"
}

$script:utf8 = [System.Text.UTF8Encoding]::new($false)
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$datasetDefinitions = @{}
foreach ($definition in $manifest.datasets) {
    $datasetDefinitions[[string]$definition.id] = $definition
}

function Write-JsonFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][object]$Value
    )

    $json = ConvertTo-Json -InputObject $Value -Depth 20
    [System.IO.File]::WriteAllText($Path, $json + [Environment]::NewLine, $script:utf8)
}

function Get-GeneratedFileEntry {
    param(
        [Parameter(Mandatory = $true)][System.IO.FileInfo]$File,
        [Parameter(Mandatory = $true)][string]$Format,
        [Parameter(Mandatory = $true)][int]$Width,
        [Parameter(Mandatory = $true)][int]$Height
    )

    return [ordered]@{
        fileName = $File.Name
        format = $Format
        width = $Width
        height = $Height
        byteLength = $File.Length
        sha256 = (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

function New-ImageSpecifications {
    param([Parameter(Mandatory = $true)][object]$Definition)

    $specifications = [System.Collections.Generic.List[object]]::new()
    if ($Definition.PSObject.Properties.Name -contains 'dimensionGroups' -and $Definition.dimensionGroups) {
        foreach ($group in $Definition.dimensionGroups) {
            for ($index = 0; $index -lt [int]$group.count; $index++) {
                $specifications.Add([pscustomobject]@{
                    format = [string]$group.format
                    width = [int]$group.width
                    height = [int]$group.height
                })
            }
        }
    }
    else {
        foreach ($format in $Definition.formatCounts.PSObject.Properties) {
            for ($index = 0; $index -lt [int]$format.Value; $index++) {
                $specifications.Add([pscustomobject]@{
                    format = [string]$format.Name
                    width = [int]$Definition.width
                    height = [int]$Definition.height
                })
            }
        }
    }

    return ,$specifications.ToArray()
}

function Write-SyntheticImage {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][int]$Width,
        [Parameter(Mandatory = $true)][int]$Height,
        [Parameter(Mandatory = $true)][int]$Index,
        [Parameter(Mandatory = $true)][int]$Seed,
        [Parameter(Mandatory = $true)][string]$Format
    )

    $bitmap = [System.Drawing.Bitmap]::new($Width, $Height)
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $red = ($Seed + ($Index * 37)) % 200 + 20
            $green = ($Seed + ($Index * 53)) % 200 + 20
            $blue = ($Seed + ($Index * 71)) % 200 + 20
            $graphics.Clear([System.Drawing.Color]::FromArgb($red, $green, $blue))

            $pen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(245, 245, 245), [Math]::Max(1.0, [Math]::Min($Width, $Height) / 96.0))
            $brush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(230, 25, 35, 45))
            try {
                for ($shape = 0; $shape -lt 8; $shape++) {
                    $x = [int](($Seed + ($Index * 31) + ($shape * 97)) % [Math]::Max(1, $Width))
                    $y = [int](($Seed + ($Index * 47) + ($shape * 61)) % [Math]::Max(1, $Height))
                    $shapeWidth = [Math]::Max(1, [int]($Width / (5 + ($shape % 3))))
                    $shapeHeight = [Math]::Max(1, [int]($Height / (5 + (($shape + 1) % 3))))
                    $boundedWidth = [Math]::Min($shapeWidth, $Width - $x)
                    $boundedHeight = [Math]::Min($shapeHeight, $Height - $y)
                    $graphics.FillEllipse($brush, $x, $y, $boundedWidth, $boundedHeight)
                    $graphics.DrawLine($pen, 0, $y, $Width - 1, $Height - $y - 1)
                }
            }
            finally {
                $brush.Dispose()
                $pen.Dispose()
            }
        }
        finally {
            $graphics.Dispose()
        }

        $imageFormat = switch ($Format) {
            'jpeg' { [System.Drawing.Imaging.ImageFormat]::Jpeg; break }
            'png' { [System.Drawing.Imaging.ImageFormat]::Png; break }
            'gif' { [System.Drawing.Imaging.ImageFormat]::Gif; break }
            'tiff' { [System.Drawing.Imaging.ImageFormat]::Tiff; break }
            default { throw "Unsupported generated image format: $Format" }
        }
        $bitmap.Save($Path, $imageFormat)
    }
    finally {
        $bitmap.Dispose()
    }
}

function Ensure-GeneratedDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$MarkerPath,
        [Parameter(Mandatory = $true)][string]$MarkerContent
    )

    if (Test-Path -LiteralPath $Path -PathType Container) {
        if (-not (Test-Path -LiteralPath $MarkerPath -PathType Leaf)) {
            throw "Refusing to write into an existing non-generated directory: $Path"
        }
        $existingMarker = Get-Content -LiteralPath $MarkerPath -Raw
        if ($existingMarker -ne $MarkerContent) {
            throw "Generated directory marker does not match this generator: $Path"
        }
    }
    else {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
        [System.IO.File]::WriteAllText($MarkerPath, $MarkerContent, $script:utf8)
    }
}

function Get-DatasetFiles {
    param([Parameter(Mandatory = $true)][string]$Path)

    return @(Get-ChildItem -LiteralPath $Path -File | Where-Object {
        $_.Name -ne 'dataset.json' -and $_.Name -ne '.generated-by-hyperbrowse'
    } | Sort-Object Name)
}

function Assert-DatasetInventory {
    param(
        [Parameter(Mandatory = $true)][object]$Definition,
        [Parameter(Mandatory = $true)][string]$DatasetRoot
    )

    $datasetMetadataPath = Join-Path $DatasetRoot 'dataset.json'
    if (-not (Test-Path -LiteralPath $datasetMetadataPath -PathType Leaf)) {
        throw "Dataset metadata is missing: $datasetMetadataPath"
    }
    $datasetMetadata = Get-Content -LiteralPath $datasetMetadataPath -Raw | ConvertFrom-Json
    if ([string]$datasetMetadata.id -ne [string]$Definition.id) {
        throw "Dataset ID mismatch in $datasetMetadataPath"
    }
    if ([string]$datasetMetadata.sourceManifestSha256 -ne $manifestHash) {
        throw "Dataset definition changed; regenerate $DatasetRoot from the current manifest."
    }

    $expectedNames = @($datasetMetadata.files | ForEach-Object { [string]$_.fileName } | Sort-Object)
    $directories = @(Get-ChildItem -LiteralPath $DatasetRoot -Directory)
    if ($directories.Count -gt 0) {
        throw "Dataset contains unexpected directories: $DatasetRoot"
    }
    $actualFiles = Get-DatasetFiles -Path $DatasetRoot
    $actualNames = @($actualFiles | ForEach-Object { $_.Name } | Sort-Object)
    if (($expectedNames -join "`n") -ne ($actualNames -join "`n")) {
        throw "Dataset $($Definition.id) has missing or unexpected files. Regenerate it in $DatasetRoot."
    }

    foreach ($entry in $datasetMetadata.files) {
        $filePath = Join-Path $DatasetRoot ([string]$entry.fileName)
        $fileInfo = Get-Item -LiteralPath $filePath
        if ([long]$entry.byteLength -ne $fileInfo.Length) {
            throw "Dataset file size changed: $filePath"
        }
        $actualHash = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ([string]$entry.sha256 -ne $actualHash) {
            throw "Dataset file checksum changed: $filePath"
        }
    }

    return $datasetMetadata
}

function New-RawDatasetFiles {
    param(
        [Parameter(Mandatory = $true)][object]$Definition,
        [Parameter(Mandatory = $true)][string]$DatasetRoot
    )

    if ([string]::IsNullOrWhiteSpace($RawSourceDirectory)) {
        $existingMetadataPath = Join-Path $DatasetRoot 'dataset.json'
        if (Test-Path -LiteralPath $existingMetadataPath -PathType Leaf) {
            $existingMetadata = Get-Content -LiteralPath $existingMetadataPath -Raw | ConvertFrom-Json
            $matchesManifest = [string]$existingMetadata.sourceManifestSha256 -eq $manifestHash
            $isAvailable = [string]$existingMetadata.status -eq 'available'
            if ($matchesManifest -and $isAvailable) {
                return [pscustomobject]@{
                    status = 'available'
                    unavailableReason = $null
                    files = @($existingMetadata.files)
                }
            }
        }
        return [pscustomobject]@{
            status = 'unavailable'
            unavailableReason = 'No local RAW source directory was supplied.'
            files = @()
        }
    }

    $rawManifestPath = Join-Path $RawSourceDirectory ([string]$Definition.rawManifestFileName)
    if (-not (Test-Path -LiteralPath $rawManifestPath -PathType Leaf)) {
        throw "RAW source requires a completed $($Definition.rawManifestFileName): $rawManifestPath"
    }
    $rawManifest = Get-Content -LiteralPath $rawManifestPath -Raw | ConvertFrom-Json
    if ([int]$rawManifest.schemaVersion -ne 1 -or [string]$rawManifest.datasetId -ne 'D') {
        throw "Unsupported RAW source manifest: $rawManifestPath"
    }
    if (@($rawManifest.files).Count -eq 0) {
        throw "RAW source manifest contains no classified cases: $rawManifestPath"
    }

    $sourceImages = @(Get-ChildItem -LiteralPath $RawSourceDirectory -File | Where-Object {
        ([string[]]$Definition.allowedExtensions) -contains $_.Extension.ToLowerInvariant()
    })
    if ($sourceImages.Count -ne @($rawManifest.files).Count) {
        throw 'RAW source manifest entries must exactly match the NEF/NRW files in the staging directory.'
    }

    $entries = [System.Collections.Generic.List[object]]::new()
    foreach ($rawCase in $rawManifest.files) {
        $fileName = [string]$rawCase.fileName
        if ([System.IO.Path]::GetFileName($fileName) -ne $fileName) {
            throw "RAW manifest fileName must be a leaf name: $fileName"
        }
        $extension = [System.IO.Path]::GetExtension($fileName).ToLowerInvariant()
        if (-not ([string[]]$Definition.allowedExtensions).Contains($extension)) {
            throw "RAW manifest has an unsupported file extension: $fileName"
        }
        if ([string]$rawCase.thumbnailPath -notin @('embedded-preview', 'full-decode', 'edge-case')) {
            throw "RAW manifest must classify thumbnailPath for $fileName"
        }
        if ([string]::IsNullOrWhiteSpace([string]$rawCase.caseId) -or [string]::IsNullOrWhiteSpace([string]$rawCase.camera)) {
            throw "RAW manifest needs caseId and camera metadata for $fileName"
        }
        if ([int]$rawCase.width -le 0 -or [int]$rawCase.height -le 0) {
            throw "RAW manifest needs positive image dimensions for $fileName"
        }

        $sourcePath = Join-Path $RawSourceDirectory $fileName
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
            throw "RAW manifest input was not found: $sourcePath"
        }
        $destinationPath = Join-Path $DatasetRoot $fileName
        Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
        $destination = Get-Item -LiteralPath $destinationPath
        $entries.Add([ordered]@{
            fileName = $destination.Name
            format = $extension.TrimStart('.')
            width = [int]$rawCase.width
            height = [int]$rawCase.height
            caseId = [string]$rawCase.caseId
            camera = [string]$rawCase.camera
            thumbnailPath = [string]$rawCase.thumbnailPath
            edgeCase = [bool]$rawCase.edgeCase
            byteLength = $destination.Length
            sha256 = (Get-FileHash -LiteralPath $destination.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        })
    }

    return [pscustomobject]@{
        status = 'available'
        unavailableReason = $null
        files = $entries.ToArray()
    }
}

$selectedDefinitions = foreach ($id in $DatasetId) {
    if (-not $datasetDefinitions.ContainsKey($id)) {
        throw "Dataset is not defined: $id"
    }
    $datasetDefinitions[$id]
}

if (-not $VerifyOnly) {
    $rootMarkerPath = Join-Path $OutputRoot '.generated-by-hyperbrowse'
    $rootMarker = "HyperBrowseBenchmarkDatasets:$($manifest.generatorVersion):$manifestHash"
    Ensure-GeneratedDirectory -Path $OutputRoot -MarkerPath $rootMarkerPath -MarkerContent $rootMarker
}

$results = [System.Collections.Generic.List[object]]::new()
$unavailableIds = [System.Collections.Generic.List[string]]::new()
foreach ($definition in $selectedDefinitions) {
    $datasetRoot = Join-Path $OutputRoot ([string]$definition.id)
    if (-not $VerifyOnly) {
        $datasetMarkerPath = Join-Path $datasetRoot '.generated-by-hyperbrowse'
        Ensure-GeneratedDirectory -Path $datasetRoot -MarkerPath $datasetMarkerPath -MarkerContent "HyperBrowseBenchmarkDataset:$($definition.id):$($manifest.generatorVersion)"

        if ([string]$definition.kind -eq 'external-raw') {
            $rawResult = New-RawDatasetFiles -Definition $definition -DatasetRoot $datasetRoot
            $fileEntries = @($rawResult.files)
            $status = [string]$rawResult.status
            $unavailableReason = $rawResult.unavailableReason
        }
        else {
            $specifications = New-ImageSpecifications -Definition $definition
            $fileEntries = [System.Collections.Generic.List[object]]::new()
            for ($index = 0; $index -lt $specifications.Count; $index++) {
                $specification = $specifications[$index]
                $extension = switch ([string]$specification.format) {
                    'jpeg' { 'jpg'; break }
                    'png' { 'png'; break }
                    'gif' { 'gif'; break }
                    'tiff' { 'tif'; break }
                    default { throw "Unsupported generated image format: $($specification.format)" }
                }
                $fileName = 'image_{0:D5}.{1}' -f ($index + 1), $extension
                $filePath = Join-Path $datasetRoot $fileName
                Write-SyntheticImage -Path $filePath -Width ([int]$specification.width) -Height ([int]$specification.height) -Index ($index + 1) -Seed ([int]$definition.seed) -Format ([string]$specification.format)
                $fileEntries.Add((Get-GeneratedFileEntry -File (Get-Item -LiteralPath $filePath) -Format ([string]$specification.format) -Width ([int]$specification.width) -Height ([int]$specification.height)))
            }
            $fileEntries = $fileEntries.ToArray()
            $status = 'available'
            $unavailableReason = $null
        }

        $totalByteLength = 0L
        foreach ($fileEntry in $fileEntries) {
            $totalByteLength += [long]$fileEntry.byteLength
        }

        $datasetMetadata = [ordered]@{
            schemaVersion = 1
            generatorVersion = [string]$manifest.generatorVersion
            sourceManifestSha256 = $manifestHash
            id = [string]$definition.id
            name = [string]$definition.name
            description = [string]$definition.description
            status = $status
            unavailableReason = $unavailableReason
            fileCount = @($fileEntries).Count
            byteLength = $totalByteLength
            files = @($fileEntries)
        }
        Write-JsonFile -Path (Join-Path $datasetRoot 'dataset.json') -Value $datasetMetadata
    }

    $verified = Assert-DatasetInventory -Definition $definition -DatasetRoot $datasetRoot
    if ([string]$verified.status -eq 'unavailable') {
        $unavailableIds.Add([string]$definition.id)
    }
    $results.Add([pscustomobject]@{
        id = [string]$definition.id
        name = [string]$definition.name
        status = [string]$verified.status
        fileCount = [int]$verified.fileCount
        byteLength = [long]$verified.byteLength
        datasetPath = $datasetRoot
    })
}

if (-not $VerifyOnly) {
    $index = [ordered]@{
        schemaVersion = 1
        generatorVersion = [string]$manifest.generatorVersion
        sourceManifestSha256 = $manifestHash
        datasets = @($results)
    }
    Write-JsonFile -Path (Join-Path $OutputRoot 'index.json') -Value $index
}

$results | Format-Table -AutoSize
if ($unavailableIds.Count -gt 0) {
    Write-Warning "RAW dataset cases are unavailable, not passed: $($unavailableIds -join ', ')"
    if ($VerifyOnly) {
        exit 2
    }
}
