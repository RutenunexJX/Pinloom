[CmdletBinding()]
param(
    [string]$BuildDirectory = "",
    [string]$OutputRoot = "",
    [string]$QtBinDirectory = "E:\QT6\6.10.2\mingw_64\bin",
    [string]$PackageVersion = "",
    [string]$PackageName = "Pinloom",
    [switch]$ReplaceExisting,
    [switch]$AllowDirty
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = (Resolve-Path (Join-Path $scriptDirectory "..\..")).Path
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $repositoryRoot "build\Desktop_Qt_6_10_2_MinGW_64_bit-Release"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path (Split-Path -Parent $repositoryRoot) "artifacts"
}
if ([string]::IsNullOrWhiteSpace($PackageVersion)) {
    $cmakeContents = Get-Content -Raw -LiteralPath (Join-Path $repositoryRoot "CMakeLists.txt")
    $versionMatch = [regex]::Match(
        $cmakeContents,
        'project\s*\(\s*Pinloom\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)',
        [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if (-not $versionMatch.Success) {
        throw "Unable to read the Pinloom version from CMakeLists.txt"
    }
    $PackageVersion = $versionMatch.Groups[1].Value
}
$PackageName = $PackageName.Trim()
if (([string]::IsNullOrWhiteSpace($PackageName)) -or
    ($PackageName -ne [System.IO.Path]::GetFileName($PackageName))) {
    throw "PackageName must be a simple directory name"
}

$sourceExecutable = Join-Path $BuildDirectory "pinloom_app.exe"
$deployTool = Join-Path $QtBinDirectory "windeployqt.exe"
if (-not (Test-Path -LiteralPath $sourceExecutable -PathType Leaf)) {
    throw "Release executable not found: $sourceExecutable"
}
if (-not (Test-Path -LiteralPath $deployTool -PathType Leaf)) {
    throw "windeployqt not found: $deployTool"
}

$revision = (& git -c core.excludesFile=/dev/null -C $repositoryRoot rev-parse --short HEAD 2>$null).Trim()
if ([string]::IsNullOrWhiteSpace($revision)) {
    $revision = "unknown"
}
$dirty = -not [string]::IsNullOrWhiteSpace(
    (& git -c core.excludesFile=/dev/null -C $repositoryRoot status --porcelain 2>$null) -join "`n")
if ($dirty -and -not $AllowDirty) {
    throw "Release packaging requires a clean Git worktree. Commit the intended source or pass -AllowDirty explicitly."
}
$buildState = if ($dirty) { "$revision-dirty" } else { $revision }

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$outputRootPath = (Resolve-Path -LiteralPath $OutputRoot).Path
$packageDirectory = Join-Path $outputRootPath $PackageName
$resolvedPackageParent = [System.IO.Path]::GetFullPath((Split-Path -Parent $packageDirectory))
if ($resolvedPackageParent -ne [System.IO.Path]::GetFullPath($outputRootPath)) {
    throw "Package directory must be an immediate child of OutputRoot"
}

$legacyOutputs = @(
    "$packageDirectory.zip",
    "$packageDirectory.zip.sha256",
    (Join-Path $outputRootPath "Pinloom-Setup-x64.exe"),
    (Join-Path $outputRootPath "Pinloom-Setup-x64.exe.sha256")
)
$hasExistingOutput = Test-Path -LiteralPath $packageDirectory
foreach ($legacyPath in $legacyOutputs) {
    $hasExistingOutput = $hasExistingOutput -or (Test-Path -LiteralPath $legacyPath)
}
if ($hasExistingOutput -and -not $ReplaceExisting) {
    throw "Package output already exists. Pass -ReplaceExisting to rebuild it."
}
if ($ReplaceExisting) {
    if (Test-Path -LiteralPath $packageDirectory) {
        Remove-Item -Recurse -Force -LiteralPath $packageDirectory
    }
    foreach ($legacyPath in $legacyOutputs) {
        if (Test-Path -LiteralPath $legacyPath -PathType Leaf) {
            Remove-Item -Force -LiteralPath $legacyPath
        }
    }
}

New-Item -ItemType Directory -Path $packageDirectory | Out-Null
Copy-Item -LiteralPath $sourceExecutable -Destination $packageDirectory

$destinationExecutable = Join-Path $packageDirectory "pinloom_app.exe"
& $deployTool `
    --release `
    --compiler-runtime `
    --no-translations `
    --dir $packageDirectory `
    $destinationExecutable
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

$sqlDriverDirectory = Join-Path $packageDirectory "sqldrivers"
if (Test-Path -LiteralPath $sqlDriverDirectory -PathType Container) {
    Get-ChildItem -File -LiteralPath $sqlDriverDirectory |
        Where-Object { $_.Name -ne "qsqlite.dll" } |
        Remove-Item -Force
}

$packageReadme = @(
    "Pinloom v$PackageVersion",
    "",
    "Build profile: Release",
    "Qt: 6.10.2",
    "Compiler: MinGW 13.1.0",
    "Source revision: $buildState",
    "Release root: $packageDirectory",
    "",
    "Run pinloom_app.exe. Keep every DLL and plugin directory beside it.",
    "Pinloom is a resident application; Shift+Space opens its command window.",
    "Configure the data directory, default root, SumatraPDF, and Obsidian paths",
    "from Pinloom Settings. User databases are not stored in this release folder."
)
$packageReadme | Set-Content -LiteralPath (Join-Path $packageDirectory "README.txt") -Encoding UTF8

$requiredFiles = @(
    "pinloom_app.exe",
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Network.dll",
    "Qt6Sql.dll",
    "Qt6Widgets.dll",
    "libgcc_s_seh-1.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll",
    "platforms\qwindows.dll",
    "sqldrivers\qsqlite.dll"
)
foreach ($relativePath in $requiredFiles) {
    $deployedPath = Join-Path $packageDirectory $relativePath
    if (-not (Test-Path -LiteralPath $deployedPath -PathType Leaf)) {
        throw "Required runtime file was not deployed: $relativePath"
    }
}

$forbiddenFiles = @(
    "Pinloom-Setup-x64.exe",
    "Pinloom.zip",
    "SHA256SUMS.txt",
    "BUILD-INFO.txt",
    "Start-Pinloom.cmd",
    "Start-Pinloom-Hidden.cmd",
    "pinloom.ico"
)
foreach ($fileName in $forbiddenFiles) {
    if (Test-Path -LiteralPath (Join-Path $packageDirectory $fileName)) {
        throw "Unexpected non-portable package file: $fileName"
    }
}

$packageFiles = Get-ChildItem -Recurse -File -LiteralPath $packageDirectory
[PSCustomObject]@{
    PackageDirectory = $packageDirectory
    PackageVersion = $PackageVersion
    SourceRevision = $buildState
    FileCount = $packageFiles.Count
    PackageBytes = ($packageFiles | Measure-Object Length -Sum).Sum
}
