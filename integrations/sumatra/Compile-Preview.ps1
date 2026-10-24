param(
    [Parameter(Mandatory = $true)][string]$ReaderSource,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$ToolsRoot = 'E:\SumatraPDF\Tools'
)
$ErrorActionPreference = 'Stop'
$revision = (& git -C $ReaderSource rev-parse HEAD).Trim()
if ($LASTEXITCODE -or $revision -ne 'b793525a841a9dc4a47598060f39510c2ee140bd') { throw 'Preview requires the pinned 3.6.1-pinloom.2 source.' }
$compiler = Join-Path $ToolsRoot 'VS2022/VC/Tools/MSVC/14.44.35207'
$sdk = Join-Path $ToolsRoot 'WindowsSDK/c'
$env:PATH = "$compiler/bin/Hostx64/x64;$env:PATH"
$env:INCLUDE = "$compiler/include;$sdk/Include/10.0.26100.0/ucrt;$sdk/Include/10.0.26100.0/shared;$sdk/Include/10.0.26100.0/um"
$env:LIB = "$compiler/lib/x64;$sdk/Lib/10.0.26100.0/ucrt/x64;$sdk/Lib/10.0.26100.0/um/x64"
$libraries = @('mupdf', 'mupdf-libs', 'zlib', 'libdjvu', 'libwebp', 'dav1d', 'libheif', 'unarrlib') | ForEach-Object { Join-Path $ReaderSource "out/rel64/$_.lib" }
foreach ($library in $libraries) { if (!(Test-Path -LiteralPath $library)) { throw "Build the paired reader first: $library" } }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$source = Join-Path $PSScriptRoot 'PinloomPdfPreview.cpp'
& "$compiler/bin/Hostx64/x64/cl.exe" /nologo /O2 /MT /EHsc /std:c++17 "/I$ReaderSource/mupdf/include" $source "/Fo$OutputDirectory/PinloomPdfPreview.obj" "/Fe$OutputDirectory/PinloomPdfPreview.exe" /link /LTCG @libraries advapi32.lib kernel32.lib user32.lib gdi32.lib comdlg32.lib shell32.lib windowscodecs.lib comctl32.lib msimg32.lib winspool.lib wininet.lib urlmon.lib gdiplus.lib ole32.lib oleAut32.lib shlwapi.lib version.lib crypt32.lib
if ($LASTEXITCODE) { throw "Preview build failed: $LASTEXITCODE" }
& (Join-Path $OutputDirectory 'PinloomPdfPreview.exe') --capabilities
if ($LASTEXITCODE) { throw 'Preview runtime validation failed.' }
