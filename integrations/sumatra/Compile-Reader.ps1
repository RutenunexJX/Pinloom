param(
    [string]$ToolsRoot = 'E:\SumatraPDF\Tools',
    [string]$Configuration = 'Release',
    [int]$Jobs = 4,
    [switch]$SkipReferences
)
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot
$vsRoot = Join-Path $ToolsRoot 'VS2022'
$compilerRoot = Join-Path $vsRoot 'VC\Tools\MSVC\14.44.35207'
$sdkRoot = Join-Path $ToolsRoot 'WindowsSDK\c'
$sdkVersion = '10.0.26100.0'
foreach ($library in @('um','ucrt')) {
    $destination = Join-Path $sdkRoot "Lib\$sdkVersion\$library\x64"
    if (!(Test-Path -LiteralPath $destination)) {
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        Copy-Item -Path (Join-Path $sdkRoot "$library\x64\*") -Destination $destination
    }
}
$env:VSInstallDir = "$vsRoot\"
$env:VCInstallDir = "$vsRoot\VC\"
$env:VCTargetsPath = "$vsRoot\MSBuild\Microsoft\VC\v170\"
$env:VCToolsInstallDir = "$compilerRoot\"
$env:WindowsSdkDir = "$sdkRoot\"
$env:WindowsSdkDir_10 = "$sdkRoot\"
$env:UniversalCRTSdkDir = "$sdkRoot\"
$env:UniversalCRTSdkDir_10 = "$sdkRoot\"
$env:WindowsSDK_ExecutablePath_x64 = "$sdkRoot\bin\$sdkVersion\x64\"
$env:PATH = "$compilerRoot\bin\Hostx64\x64;$env:WindowsSDK_ExecutablePath_x64;$env:PATH"
$includePath = "$compilerRoot\include%3B$compilerRoot\atlmfc\include%3B$sdkRoot\Include\$sdkVersion\ucrt%3B$sdkRoot\Include\$sdkVersion\shared%3B$sdkRoot\Include\$sdkVersion\um%3B$sdkRoot\Include\$sdkVersion\winrt"
$libraryPath = "$compilerRoot\lib\x64%3B$compilerRoot\atlmfc\lib\x64%3B$sdkRoot\Lib\$sdkVersion\ucrt\x64%3B$sdkRoot\Lib\$sdkVersion\um\x64"
Push-Location $sourceRoot
try {
    & '.\bin\premake5.exe' vs2022
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    $extra = @()
    if ($SkipReferences) { $extra += '/p:BuildProjectReferences=false' }
    & "$vsRoot\MSBuild\Current\Bin\MSBuild.exe" '.\vs2022\SumatraPDF.vcxproj' /nologo /verbosity:minimal "/m:$Jobs" "/p:Configuration=$Configuration" /p:Platform=x64 /p:PlatformToolset=v143 /p:PreferredToolArchitecture=x64 /p:VCToolsVersion=14.44.35207 "/p:WindowsTargetPlatformVersion=$sdkVersion" "/p:UCRTVersion=$sdkVersion" "/p:IncludePath=$includePath" "/p:LibraryPath=$libraryPath" /p:TrackFileAccess=false /nodeReuse:false @extra
    exit $LASTEXITCODE
} finally { Pop-Location }
