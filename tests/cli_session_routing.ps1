param(
    [Parameter(Mandatory=$true)][string]$BinDirectory,
    [string]$WorkDirectory = (Join-Path ([IO.Path]::GetTempPath()) ('Pinloom sessions ' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'cli_process.ps1')
$bin = [IO.Path]::GetFullPath($BinDirectory)
$work = [IO.Path]::GetFullPath($WorkDirectory)
[IO.Directory]::CreateDirectory($work) | Out-Null
$cli = Join-Path $bin 'pinloom_cli.exe'
$gui = Join-Path $bin 'pinloom_app.exe'
$libraries = @((Join-Path $work '会话 A'), (Join-Path $work '会话 B'))
$evidence = [Collections.Generic.List[object]]::new()
function Run([string]$Library, [string]$Method, [hashtable]$Params=@{}, [int]$Expected=0) {
    $inputFile = Join-Path $work 'input.json'
    [IO.File]::WriteAllText($inputFile, ($Params | ConvertTo-Json -Compress), [Text.UTF8Encoding]::new($false))
    $invocation = Invoke-PinloomProcess $cli @($Method, '--library', $Library, '--input', $inputFile)
    $response = $invocation.Stdout | ConvertFrom-Json
    $evidence.Add(@{method=$Method;response=$response})
    if ($invocation.ExitCode -ne $Expected -or $response.exitCode -ne $Expected) { throw "$Method returned $($invocation.ExitCode): $($invocation.Stdout) $($invocation.Stderr)" }
    return $response
}
$sessions = @()
try {
    foreach ($library in $libraries) {
        if (Test-Path -LiteralPath (Join-Path $library 'pinloom.sqlite3')) { throw 'Choose a new WorkDirectory' }
        Run $library 'library.init' | Out-Null
        Start-Process -FilePath $gui -ArgumentList @('--library', ('"'+$library+'"'), '--no-global-hooks') -WindowStyle Hidden | Out-Null
        $deadline = [DateTime]::UtcNow.AddSeconds(20)
        do {
            Start-Sleep -Milliseconds 100
            $probe = Invoke-PinloomProcess $cli @('session.status', '--library', $library, '--require-gui')
            $ready = $probe.Stdout | ConvertFrom-Json
        } until (($probe.ExitCode -eq 0 -and $ready.ok) -or [DateTime]::UtcNow -gt $deadline)
        if ($probe.ExitCode -ne 0 -or -not $ready.ok) { throw 'Selected GUI session did not start' }
        $sessions += $ready.session.id
    }
    if ($sessions[0] -eq $sessions[1]) { throw 'Distinct libraries resolved to the same session' }
    $a = Run $libraries[0] 'clip.create' @{name='Scoped sample';text='Session A';ifVersion='absent'}
    $b = Run $libraries[1] 'clip.create' @{name='Scoped sample';text='Session B';ifVersion='absent'}
    if ($a.session.id -ne $sessions[0] -or $b.session.id -ne $sessions[1]) { throw 'Write routed to wrong session' }
    Run $libraries[1] 'clip.get' @{id=$a.result.id} 3 | Out-Null
    $viewA = Run $libraries[0] 'view.open' @{view='clips';clipId=$a.result.id}
    $viewB = Run $libraries[1] 'view.open' @{view='clips';clipId=$b.result.id}
    if (-not $viewA.result.selectionFound -or -not $viewB.result.selectionFound) { throw 'Session-specific GUI selection failed' }
    Run $libraries[0] 'session.close' | Out-Null
    $stillB = Run $libraries[1] 'clip.get' @{id=$b.result.id}
    if ($stillB.result.text -ne 'Session B' -or $stillB.session.id -ne $sessions[1]) { throw 'Closing A affected B' }
    @{passed=$true;sessions=$sessions;requests=$evidence.Count} | ConvertTo-Json -Compress
} finally {
    foreach ($library in $libraries) { Invoke-PinloomProcess $cli @('session.close', '--library', $library, '--require-gui') | Out-Null }
    $evidence | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $work 'routing-evidence.json') -Encoding UTF8
}
