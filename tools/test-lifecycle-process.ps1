param([string]$OutputDirectory = "$PSScriptRoot/../build/lifecycle-diagnostics/process-tests")
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lifecycle-process.ps1"
Initialize-Lifecycle (Join-Path $OutputDirectory ([Guid]::NewGuid().ToString('N')))
$shell = (Get-Command pwsh -ErrorAction Stop).Source
$child = Join-Path $script:LifecycleDirectory 'child.ps1'
$parent = Join-Path $script:LifecycleDirectory 'parent.ps1'
Set-Content -LiteralPath $child 'while ($true) { Start-Sleep -Milliseconds 100 }'
@'
param($Shell, $Child, $PidFile, $Mode)
$p = Start-Process -FilePath $Shell -ArgumentList ('-NoProfile -File "{0}"' -f $Child) -WindowStyle Hidden -PassThru
Set-Content -LiteralPath $PidFile $p.Id
if ($Mode -eq 'stay') { while ($true) { Start-Sleep -Milliseconds 100 } }
exit 0
'@ | Set-Content -LiteralPath $parent
$sentinel = Start-LifecycleProcess $shell ('-NoProfile -File "{0}"' -f $child) sentinel 30
try {
    foreach ($mode in @('exit','stay')) {
        $pidFile = Join-Path $script:LifecycleDirectory "$mode.pid"
        $operation = Start-LifecycleProcess $shell ('-NoProfile -File "{0}" "{1}" "{2}" "{3}" {4}' -f $parent,$shell,$child,$pidFile,$mode) "timeout-$mode" 4
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $timeout = $false
        try { $null = Complete-LifecycleProcess $operation }
        catch { if ($_.Exception.ToString() -notmatch 'timed out') { throw }; $timeout = $true }
        if (-not $timeout) { throw 'Expected process tree timeout, got success' }
        if ($watch.Elapsed.TotalSeconds -gt 12) { throw 'Timeout cleanup exceeded bound' }
        if (-not (Test-Path $pidFile)) { throw 'Child did not start; timeout regression was not exercised' }
        $childId = [int](Get-Content -LiteralPath $pidFile)
        if (Get-Process -Id $childId -ErrorAction SilentlyContinue) { throw 'Owned child survived cleanup' }
        if ($sentinel.Process.HasExited) { throw 'Unrelated process was terminated' }
        $report = Get-Content "$($operation.Prefix).process.log" -Raw
        if ($report -notmatch 'TIMEOUT' -or $report -notmatch 'TERMINATE owned job only') { throw 'Missing timeout diagnostics' }
        if ($mode -eq 'exit' -and $report -notmatch 'exited=True code=0') { throw 'Parent-exits-first regression was not exercised' }
        Write-Lifecycle "PASS: $mode parent and child timeout; unrelated process preserved; duration=$($watch.Elapsed.TotalSeconds)"
    }
    $null = Invoke-LifecycleProcess $shell '-NoProfile -Command "exit 7"' expected-failure 10 7
    Write-Lifecycle 'PASS: expected nonzero exit is checked'
    $rejected = $false
    try { $null = Invoke-LifecycleProcess $shell '-NoProfile -Command "exit 7"' unexpected-failure 10 }
    catch {
        if ($_.Exception.Message -notmatch 'Expected exit 0, got 7') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'Unexpected exit was accepted as success' }
    Write-Lifecycle 'PASS: unexpected nonzero exit fails the operation'
} finally { Stop-LifecycleProcess $sentinel }
Write-Output "Process tests retained at $script:LifecycleDirectory"
