param(
    [Parameter(Mandatory=$true)][string]$BinDirectory,
    [string]$WorkDirectory = (Join-Path ([IO.Path]::GetTempPath()) ('Pinloom CLI 样例 ' + [Guid]::NewGuid().ToString('N'))),
    [switch]$WithGui
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'cli_process.ps1')
$bin = [IO.Path]::GetFullPath($BinDirectory)
$work = [IO.Path]::GetFullPath($WorkDirectory)
$cli = Join-Path $bin 'pinloom_cli.exe'
$gui = Join-Path $bin 'pinloom_app.exe'
$library = Join-Path $work '资料 库'
if (Test-Path -LiteralPath (Join-Path $library 'pinloom.sqlite3')) { throw 'Choose a new WorkDirectory; this example never overwrites a library.' }
[IO.Directory]::CreateDirectory($work) | Out-Null
$transcript = Join-Path $work 'commands.jsonl'
$inputFile = Join-Path $work 'input.json'
$script:count = 0
function Invoke-Pinloom([string]$Method, [hashtable]$Params = @{}, [int]$Expected = 0) {
    [IO.File]::WriteAllText($inputFile, ($Params | ConvertTo-Json -Depth 20 -Compress), [Text.UTF8Encoding]::new($false))
    $invocation = Invoke-PinloomProcess $cli @($Method, '--library', $library, '--input', $inputFile)
    $raw = $invocation.Stdout
    $exit = $invocation.ExitCode
    $response = $raw | ConvertFrom-Json
    [IO.File]::AppendAllText($transcript, ((@{ method=$Method; response=$response } | ConvertTo-Json -Depth 30 -Compress) + "`n"), [Text.UTF8Encoding]::new($false))
    $script:count++
    if ($exit -ne $Expected -or $response.exitCode -ne $Expected) { throw "$Method returned $exit : $raw $($invocation.Stderr)" }
    return $response
}
function Assert-True([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }

$init = Invoke-Pinloom 'library.init'
$status = Invoke-Pinloom 'session.status'
Assert-True ($status.result.version -eq $init.result.version) 'Reopened empty library version changed'
$process = $null
try {
    if ($WithGui) {
        $process = Start-Process -FilePath $gui -ArgumentList @('--library', ('"' + $library + '"'), '--no-global-hooks') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $work 'gui.stdout.log') -RedirectStandardError (Join-Path $work 'gui.stderr.log')
        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        do {
            Start-Sleep -Milliseconds 250
            $probe = Invoke-PinloomProcess $cli @('session.status', '--library', $library, '--require-gui')
            $ready = $probe.ExitCode -eq 0
        } until ($ready -or [DateTime]::UtcNow -gt $deadline -or $process.HasExited)
        Assert-True $ready 'GUI command session did not become ready'
        $status = Invoke-Pinloom 'session.status'
        Assert-True ($status.session.mode -eq 'gui') 'CLI did not route to the selected GUI'
    }
    $sample = Join-Path $work '例子 文件.sv'
    [IO.File]::WriteAllText($sample, "module sample;`nwire ready;`nendmodule`n", [Text.UTF8Encoding]::new($false))
    $create = @{ name='入口锚点'; aliases=@('入口 alias'); tags=@('设计'); pinned=$true; targetFile=$sample; locator=@{type='file.line'; line=2}; ifVersion='absent' }
    $preview = Invoke-Pinloom 'anchor.create' ($create + @{dryRun=$true})
    $none = Invoke-Pinloom 'search' @{query='入口 alias'}
    Assert-True ($none.result.items.Count -eq 0) 'Dry run persisted an anchor'
    $created = Invoke-Pinloom 'anchor.create' $create
    $anchor = $created.result
    $key = @{resourceId=$anchor.resourceId; id=$anchor.id}
    if ($WithGui) {
        Assert-True $created.guiAdopted 'GUI did not adopt the write'
        $view = Invoke-Pinloom 'view.open' @{view='anchors'; resourceId=$anchor.resourceId; anchorId=$anchor.id}
        Assert-True $view.result.selectionFound 'Created anchor was not selectable in the actual GUI'
        Assert-True ($view.result.anchors.anchors.name -contains '入口锚点') 'Created anchor missing from displayed rows'
    }
    $updated = Invoke-Pinloom 'anchor.update' ($key + @{ifVersion=$anchor.version; name='修改锚点'; aliases=@('新 alias'); tags=@('设计','已修改'); pinned=$false; locator=@{type='file.line'; line=3}})
    $conflict = Invoke-Pinloom 'anchor.update' ($key + @{ifVersion=$anchor.version; name='过期内容'}) 4
    $invalid = Invoke-Pinloom 'anchor.update' ($key + @{ifVersion=$updated.result.version; name='不应保存'; locator=@{type='file.line'; line=-1}}) 2
    $read = Invoke-Pinloom 'anchor.get' $key
    Assert-True ($read.result.name -eq '修改锚点' -and $read.result.version -eq $updated.result.version) 'Rejected update changed the stored anchor'
    $found = Invoke-Pinloom 'search' @{query='新 alias'; kind='anchors'}
    Assert-True ($found.result.items.anchorId -contains $anchor.id) 'Updated alias was not searchable'
    $valid = Invoke-Pinloom 'anchor.validate' $key
    Assert-True $valid.result.valid 'Valid locator was rejected'
    $resolved = Invoke-Pinloom 'entry.resolve' @{resourceId=$anchor.resourceId; anchorId=$anchor.id}
    Assert-True ($resolved.result.state -eq 'resolved') 'Resolve did not report a resolved target'
    if ($WithGui) {
        $view = Invoke-Pinloom 'view.inspect'
        Assert-True ($view.result.anchors.anchors.name -contains '修改锚点') 'Existing GUI did not refresh the renamed anchor'
        $opened = Invoke-Pinloom 'entry.open' @{resourceId=$anchor.resourceId; anchorId=$anchor.id}
        Start-Sleep -Milliseconds 300
        $operation = Invoke-Pinloom 'operation.get' @{id=$opened.result.id}
        Assert-True ($operation.result.state -eq 'located' -and $operation.result.positionVerified) 'Built-in preview did not verify the requested line'
    } else {
        $unavailable = Invoke-Pinloom 'entry.open' @{resourceId=$anchor.resourceId; anchorId=$anchor.id} 5
    }
    $fresh = Invoke-Pinloom 'anchor.get' $key
    $deleted = Invoke-Pinloom 'anchor.delete' ($key + @{ifVersion=$fresh.result.version})
    $trash = Invoke-Pinloom 'search' @{query='修改锚点'; scope='trash'}
    Assert-True ($trash.result.items.anchorId -contains $anchor.id) 'Trashed anchor not searchable in trash scope'
    $restored = Invoke-Pinloom 'anchor.restore' ($key + @{ifVersion=$deleted.result.version})
    Assert-True (-not $restored.result.deleted) 'Restore failed'
    $clip = Invoke-Pinloom 'clip.create' @{name='保存片段'; text="alpha`n中文片段"; aliases=@('clip alias'); tags=@('工程'); ifVersion='absent'}
    $clipEdit = Invoke-Pinloom 'clip.update' @{id=$clip.result.id; ifVersion=$clip.result.version; text='已修改的片段'; pinned=$true}
    $clipConflict = Invoke-Pinloom 'clip.update' @{id=$clip.result.id; ifVersion=$clip.result.version; text='stale'} 4
    $clipRead = Invoke-Pinloom 'clip.get' @{id=$clip.result.id}
    Assert-True ($clipRead.result.text -eq '已修改的片段') 'Clip update was not persisted'
    if ($WithGui) {
        $clipView = Invoke-Pinloom 'view.open' @{view='clips'; clipId=$clip.result.id}
        Assert-True $clipView.result.selectionFound 'Saved Clip was not selectable in the existing GUI session'
    }
    $clipDeleted = Invoke-Pinloom 'clip.delete' @{id=$clip.result.id; ifVersion=$clipEdit.result.version}
    $clipRestored = Invoke-Pinloom 'clip.restore' @{id=$clip.result.id; ifVersion=$clipDeleted.result.version}
    $history = Invoke-Pinloom 'history.list' @{limit=10}
    Assert-True ($history.result.items.Count -eq 0) 'Isolated profile unexpectedly captured clipboard history'
    $state = Invoke-Pinloom 'session.status'
    $prune = Invoke-Pinloom 'history.prune' @{keep=10; ttlSeconds=3600; ifVersion=$state.result.version}
    $inbox = Invoke-Pinloom 'inbox.add' @{path=$sample; name='已选文件'; mode='link'; ifVersion='absent'; pinned=$true}
    Assert-True $inbox.result.pinned 'Inbox pin was not persisted atomically'
    $root = Invoke-Pinloom 'root.register' @{path=$work; displayName='样例目录'; ignoredDirectoryNames=@('ignored'); ifVersion='absent'}
    $rootFiles = Invoke-Pinloom 'root.files' @{id=$root.result.id; limit=50}
    Assert-True ($rootFiles.result.items.name -contains '例子 文件.sv') 'Registered root browsing missed the sample'
    $escaped = Invoke-Pinloom 'root.files' @{id=$root.result.id; relativePath='..'} 4
    $scoped = Invoke-Pinloom 'root.query' @{id=$root.result.id; query='已选文件'}
    Assert-True ($scoped.result.items.resourceId -contains $inbox.result.id) 'Registered-root query missed the Inbox file'
    $archive = Join-Path $work '导出 资料.json'
    $exported = Invoke-Pinloom 'library.export' @{path=$archive}
    $state = Invoke-Pinloom 'session.status'
    $previewImport = Invoke-Pinloom 'library.import' @{path=$archive; mode='merge'; ifVersion=$state.result.version; dryRun=$true}
    $stateAfter = Invoke-Pinloom 'session.status'
    Assert-True ($stateAfter.result.version -eq $state.result.version) 'Import dry run changed persistent state'
    $imported = Invoke-Pinloom 'library.import' @{path=$archive; mode='merge'; ifVersion=$state.result.version}
    $notFound = Invoke-Pinloom 'anchor.get' @{resourceId=$anchor.resourceId; id='missing-id'} 3
    if ($WithGui) {
        $closed = Invoke-Pinloom 'session.close'
        Assert-True ($process.WaitForExit(10000)) 'GUI did not close cleanly'
        $reopened = Invoke-Pinloom 'clip.get' @{id=$clip.result.id}
        Assert-True ($reopened.session.mode -eq 'headless' -and $reopened.result.text -eq '已修改的片段') 'GUI-persisted Clip did not survive close and reopen'
    }
    $summary = @{passed=$true; commands=$script:count; gui=[bool]$WithGui; library=$library; transcript=$transcript}
    $summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $work 'summary.json') -Encoding UTF8
    $summary | ConvertTo-Json -Compress
} finally {
    if ($process -and -not $process.HasExited) {
        Invoke-PinloomProcess $cli @('session.close', '--library', $library, '--require-gui') | Out-Null
        # A dirty or modal session is deliberately left open for inspection.
    }
}
