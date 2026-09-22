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
$cache = Get-Content -LiteralPath (Join-Path $BuildDirectory "CMakeCache.txt")
$backendEntry = $cache | Where-Object { $_ -match '^PINLOOM_UI_BACKEND:STRING=(.+)$' } | Select-Object -First 1
$controlBackend = if ($backendEntry) { $backendEntry.Substring($backendEntry.IndexOf('=') + 1).ToUpperInvariant() }
    elseif ($cache -match '^PINLOOM_ENABLE_SUITEUI:BOOL=ON$') { 'SUITEUI' } else { 'CLASSIC' }
if ($controlBackend -notin @('ELA', 'SUITEUI', 'CLASSIC')) { throw "Unknown control backend: $controlBackend" }
$usesSuiteUi = $controlBackend -eq 'SUITEUI'
$usesEla = $controlBackend -eq 'ELA'
$elaNotices = Join-Path $BuildDirectory 'notices/ElaWidgetTools'
if ($usesEla) {
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDirectory 'ElaWidgetTools.dll') -PathType Leaf)) {
        throw 'Missing ElaWidgetTools.dll in the selected build.'
    }
    foreach ($name in @('LICENSE', 'FontAwesome-LICENSE.txt', 'UPSTREAM-REVISION.md')) {
        if (-not (Test-Path -LiteralPath (Join-Path $elaNotices $name) -PathType Leaf)) { throw "Missing Ela notice: $name" }
    }
}
$sdkNotices = ''
if ($usesSuiteUi) {
    $entry = $cache | Where-Object { $_ -match '^PINLOOM_SUITEUI_NOTICES_DIR:INTERNAL=(.+)$' } | Select-Object -First 1
    if (-not $entry) { throw 'Reconfigure this SDK build to record its notice directory.' }
    $sdkNotices = $entry.Substring($entry.IndexOf('=') + 1)
    foreach ($name in @('NOTICE.txt', 'SuiteUi-Apache-2.0.txt', 'Qlementine-MIT.txt', 'Inter-OFL.txt',
                       'RobotoMono-Apache-2.0.txt', 'UPSTREAM.md', 'font-metadata.json', 'stop-all.patch', 'build-info.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $sdkNotices $name) -PathType Leaf)) { throw "Missing SDK notice: $name" }
    }
}
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
if ($usesEla) {
    Copy-Item -LiteralPath (Join-Path $BuildDirectory 'ElaWidgetTools.dll') -Destination $packageDirectory
    $licenseDirectory = Join-Path $packageDirectory 'licenses/ElaWidgetTools'
    New-Item -ItemType Directory -Path $licenseDirectory -Force | Out-Null
    Get-ChildItem -LiteralPath $elaNotices -File | Copy-Item -Destination $licenseDirectory
}
if ($usesSuiteUi) {
    $licenseDirectory = Join-Path $packageDirectory 'licenses/SuiteUi'
    New-Item -ItemType Directory -Path $licenseDirectory -Force | Out-Null
    Get-ChildItem -LiteralPath $sdkNotices -File | Copy-Item -Destination $licenseDirectory
}
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
    "Default UI: $controlBackend",
    "Qt: 6.10.2",
    "Compiler: MinGW 13.1.0",
    "Source revision: $buildState",
    "Release root: $packageDirectory",
    "",
    "Run pinloom_app.exe. Keep every DLL and plugin directory beside it.",
    "Pinloom is a resident application; Shift+Space opens its command window.",
    "Set PINLOOM_UI_STYLE=classic before startup to use the original controls.",
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
