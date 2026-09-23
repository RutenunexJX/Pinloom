[CmdletBinding()]
param(
    [string]$BuildDirectory = "",
    [string]$OutputRoot = "",
    [string]$QtBinDirectory = "E:\QT6\6.10.2\mingw_64\bin",
    [string]$QtLicenseFile = "",
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
$sourcePdfProbe = Join-Path $BuildDirectory "pinloom_pdf_probe.exe"
if (-not (Test-Path -LiteralPath $sourcePdfProbe -PathType Leaf)) {
    throw 'Missing pinloom_pdf_probe.exe; build the PDF capture helper before packaging.'
}
$cache = Get-Content -LiteralPath (Join-Path $BuildDirectory "CMakeCache.txt")
$backendEntry = $cache | Where-Object { $_ -match '^PINLOOM_UI_BACKEND:STRING=(.+)$' } | Select-Object -First 1
$controlBackend = if ($backendEntry) { $backendEntry.Substring($backendEntry.IndexOf('=') + 1).ToUpperInvariant() } else { '' }
if ($controlBackend -ne 'ELA') { throw 'Only the ELA control backend is supported.' }
$usesEla = $controlBackend -eq 'ELA'
$elaNotices = Join-Path $BuildDirectory 'notices/ElaWidgetTools'
if ($usesEla) {
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDirectory 'ElaWidgetTools.dll') -PathType Leaf)) {
        throw 'Missing ElaWidgetTools.dll in the selected build.'
    }
    foreach ($name in @('LICENSE', 'FontAwesome-LICENSE.txt', 'UPSTREAM-REVISION.md', 'patches/12-pinloom-native-interactions.patch')) {
        if (-not (Test-Path -LiteralPath (Join-Path $elaNotices $name) -PathType Leaf)) { throw "Missing Ela notice: $name" }
    }
}
if ([string]::IsNullOrWhiteSpace($QtLicenseFile)) {
    $QtLicenseFile = Join-Path $QtBinDirectory '..\..\..\Licenses\LICENSE'
}
$compilerEntry = $cache | Where-Object { $_ -match '^CMAKE_CXX_COMPILER:(FILEPATH|STRING)=(.+)$' } | Select-Object -First 1
if (-not $compilerEntry) { throw 'Missing compiler path in build cache.' }
$compiler = $compilerEntry.Substring($compilerEntry.IndexOf('=') + 1)
$compilerLicenses = Join-Path (Split-Path -Parent $compiler) '..\licenses'
$runtimeNotices = @{
    'Qt-LICENSE.txt' = $QtLicenseFile
    'GCC-COPYING.RUNTIME.txt' = (Join-Path $compilerLicenses 'gcc/COPYING.RUNTIME')
    'GCC-COPYING3.LIB.txt' = (Join-Path $compilerLicenses 'gcc/COPYING3.LIB')
    'GCC-COPYING3.txt' = (Join-Path $compilerLicenses 'gcc/COPYING3')
    'MinGW-w64-COPYING.txt' = (Join-Path $compilerLicenses 'mingw-w64/COPYING')
    'winpthreads-COPYING.txt' = (Join-Path $compilerLicenses 'winpthreads/COPYING')
}
foreach ($source in $runtimeNotices.Values) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing runtime notice: $source" }
}
$deployTool = Join-Path $QtBinDirectory "windeployqt.exe"
if (-not (Test-Path -LiteralPath $sourceExecutable -PathType Leaf)) {
    throw "Release executable not found: $sourceExecutable"
}
if (-not (Test-Path -LiteralPath $deployTool -PathType Leaf)) {
    throw "windeployqt not found: $deployTool"
}

$fullRevision = (& git -c core.excludesFile=/dev/null -C $repositoryRoot rev-parse HEAD 2>$null).Trim()
$revision = (& git -c core.excludesFile=/dev/null -C $repositoryRoot rev-parse --short HEAD 2>$null).Trim()
if ([string]::IsNullOrWhiteSpace($revision)) {
    $revision = "unknown"
}
$dirty = -not [string]::IsNullOrWhiteSpace(
    (& git -c core.excludesFile=/dev/null -C $repositoryRoot status --porcelain 2>$null) -join "`n")
if ($dirty -and -not $AllowDirty) {
    throw "Release packaging requires a clean Git worktree. Commit the intended source or pass -AllowDirty explicitly."
}
if (-not $AllowDirty) {
    if ($cache -notcontains "PINLOOM_BUILD_SOURCE_REVISION:INTERNAL=$fullRevision" -or
        $cache -notcontains 'PINLOOM_BUILD_SOURCE_CLEAN:INTERNAL=ON' -or
        $cache -notcontains "PINLOOM_BUILD_VERSION:INTERNAL=$PackageVersion") {
        throw 'Build source does not match the clean release. Reconfigure, rebuild and test after committing.'
    }
    if ($cache -notcontains 'CMAKE_BUILD_TYPE:STRING=Release') { throw 'A Release build is required.' }
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
Copy-Item -LiteralPath $sourcePdfProbe -Destination $packageDirectory

$destinationExecutable = Join-Path $packageDirectory "pinloom_app.exe"
if ($usesEla) {
    Copy-Item -LiteralPath (Join-Path $BuildDirectory 'ElaWidgetTools.dll') -Destination $packageDirectory
    $licenseDirectory = Join-Path $packageDirectory 'licenses/ElaWidgetTools'
    New-Item -ItemType Directory -Path $licenseDirectory -Force | Out-Null
    Get-ChildItem -LiteralPath $elaNotices | Copy-Item -Destination $licenseDirectory -Recurse
}
foreach ($entry in $runtimeNotices.GetEnumerator()) {
    Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $packageDirectory "licenses/$($entry.Key)")
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

@('[Paths]', 'Prefix=.', 'Plugins=.') |
    Set-Content -LiteralPath (Join-Path $packageDirectory 'qt.conf') -Encoding ASCII

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
    "Package staging root: $packageDirectory",
    "",
    "Run pinloom_app.exe. Keep every DLL and plugin directory beside it.",
    "Pinloom is a resident application; Shift+Space opens its command window.",
    "Only the Ela UI is supported; leave PINLOOM_UI_STYLE unset or set it to ela.",
    "Configure the data directory, default root, SumatraPDF, and Obsidian paths",
    "from Pinloom Settings. User databases are not stored in this release folder."
)
$packageReadme | Set-Content -LiteralPath (Join-Path $packageDirectory "README.txt") -Encoding UTF8

$requiredFiles = @(
    "pinloom_app.exe",
    "pinloom_pdf_probe.exe",
    "qt.conf",
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
$metadataPath = Join-Path $outputRootPath 'release-metadata.json'
[ordered]@{
    schemaVersion = 1
    application = 'Pinloom'
    version = $PackageVersion
    revision = $fullRevision
    branch = ((& git -C $repositoryRoot branch --show-current) -join '').Trim()
    tags = @(& git -C $repositoryRoot tag --points-at HEAD)
    origin = ((& git -C $repositoryRoot remote get-url origin) -join '').Trim()
    clean = -not $dirty
    configuration = 'Release'
    qtVersion = '6.10.2'
    compiler = $compiler
    backend = $controlBackend
    databaseSchema = 16
    packageDirectory = $packageDirectory
    fileCount = $packageFiles.Count
    generatedUtc = [DateTime]::UtcNow.ToString('o')
    elaUpstream = '454cac2d57a47d3cc28577dc817793aec1881ca7'
    elaSharedBaseline = '75180fad5e5f5142684cf092649deffe5720994d'
    elaSharedPatchLevel = 29
    elaSharedPatch29Sha256 = 'c292256d9d23cc391b2a185b88d7335f79410ef08e491f727916829c627a88f8'
    elaPinloomPatch = '12-pinloom-native-interactions.patch'
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $metadataPath -Encoding UTF8
$hashFiles = @($packageFiles) + @(Get-Item -LiteralPath $metadataPath)
$hashFiles | Sort-Object FullName | ForEach-Object {
    $relative = [IO.Path]::GetRelativePath($outputRootPath, $_.FullName).Replace('\', '/')
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
} | Set-Content -LiteralPath (Join-Path $outputRootPath 'SHA256SUMS.txt') -Encoding ASCII
[PSCustomObject]@{
    PackageDirectory = $packageDirectory
    PackageVersion = $PackageVersion
    SourceRevision = $buildState
    FileCount = $packageFiles.Count
    PackageBytes = ($packageFiles | Measure-Object Length -Sum).Sum
}
