[CmdletBinding()]
param(
    [string]$BuildDirectory = "",
    [string]$OutputRoot = "",
    [string]$QtBinDirectory = "E:\QT6\6.10.2\mingw_64\bin",
    [string]$InnoCompiler = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    [string]$PackageVersion = (Get-Date -Format "yyyyMMdd-HHmmss"),
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
$packageName = "Pinloom-Windows-x64-$PackageVersion"
$packageDirectory = Join-Path $OutputRoot $packageName
$archivePath = "$packageDirectory.zip"
if ((Test-Path -LiteralPath $packageDirectory) -or (Test-Path -LiteralPath $archivePath)) {
    throw "Package output already exists: $packageName"
}

New-Item -ItemType Directory -Path $packageDirectory | Out-Null
Copy-Item -LiteralPath $sourceExecutable -Destination $packageDirectory
Copy-Item -LiteralPath (Join-Path $scriptDirectory "Start-Pinloom.cmd") -Destination $packageDirectory
Copy-Item -LiteralPath (Join-Path $scriptDirectory "Start-Pinloom-Hidden.cmd") -Destination $packageDirectory
Copy-Item -LiteralPath (Join-Path $scriptDirectory "README.txt") -Destination $packageDirectory

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

@(
    "Package: $packageName"
    "Source revision: $buildState"
    "Built at: $((Get-Date).ToString('yyyy-MM-ddTHH:mm:ssK'))"
    "Architecture: Windows x64"
) | Set-Content -LiteralPath (Join-Path $packageDirectory "BUILD-INFO.txt") -Encoding UTF8

$manifestLines = Get-ChildItem -Recurse -File -LiteralPath $packageDirectory |
    Where-Object { $_.Name -ne "SHA256SUMS.txt" } |
    Sort-Object FullName |
    ForEach-Object {
        $relativePath = $_.FullName.Substring($packageDirectory.Length).TrimStart('\')
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()
        "$hash  $relativePath"
    }
$manifestLines | Set-Content -LiteralPath (Join-Path $packageDirectory "SHA256SUMS.txt") -Encoding ASCII

Compress-Archive -Path (Join-Path $packageDirectory "*") `
                 -DestinationPath $archivePath `
                 -CompressionLevel Optimal
$archiveHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath).Hash.ToLowerInvariant()
$archiveHash | Set-Content -LiteralPath "$archivePath.sha256" -Encoding ASCII

$installerPath = ""
$installerHash = ""
if (Test-Path -LiteralPath $InnoCompiler -PathType Leaf) {
    $installerScript = Join-Path $scriptDirectory "Pinloom.iss"
    & $InnoCompiler `
        "/DMyAppVersion=$PackageVersion" `
        "/DPackageSource=$packageDirectory" `
        "/DPackageOutput=$OutputRoot" `
        $installerScript
    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup failed with exit code $LASTEXITCODE"
    }
    $installerPath = Join-Path $OutputRoot "Pinloom-Setup-x64-$PackageVersion.exe"
    if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
        throw "Installer output not found: $installerPath"
    }
    $installerHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $installerPath).Hash.ToLowerInvariant()
    $installerHash | Set-Content -LiteralPath "$installerPath.sha256" -Encoding ASCII
}

[PSCustomObject]@{
    PackageDirectory = $packageDirectory
    ArchivePath = $archivePath
    ArchiveSha256 = $archiveHash
    InstallerPath = $installerPath
    InstallerSha256 = $installerHash
    SourceRevision = $buildState
}
