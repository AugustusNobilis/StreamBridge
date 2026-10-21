# SPDX-License-Identifier: GPL-2.0-or-later
if (-not ('StreamBridge.Tests.ProcessJob' -as [type])) {
    Add-Type -Path "$PSScriptRoot/lifecycle-process.cs"
}
function Write-Lifecycle([string]$Message) {
    $line = [DateTime]::UtcNow.ToString('O') + ' ' + $Message
    Add-Content -LiteralPath "$script:LifecycleDirectory/lifecycle.log" -Value $line -Encoding utf8
    Write-Host $line
}
function Initialize-Lifecycle([string]$Directory) {
    $script:LifecycleDirectory = [IO.Path]::GetFullPath($Directory)
    New-Item -ItemType Directory -Force $script:LifecycleDirectory | Out-Null
    $script:LifecycleSequence = 0
}
function Start-LifecycleProcess {
    param([string]$FilePath, [string]$Arguments = '', [string]$Name,
          [ValidateRange(1,1800)][int]$TimeoutSeconds = 120)
    $script:LifecycleSequence++
    $prefix = Join-Path $script:LifecycleDirectory ('{0:D3}-{1}' -f $script:LifecycleSequence, $Name)
    $nativeLog = "$prefix.native.log"
    Write-Lifecycle "START $Name timeout=${TimeoutSeconds}s executable=$FilePath"
    $previousLog = $env:STREAMBRIDGE_LIFECYCLE_LOG
    try {
        $env:STREAMBRIDGE_LIFECYCLE_LOG = $nativeLog
        $job = [StreamBridge.Tests.ProcessJob]::new((Resolve-Path -LiteralPath $FilePath).Path,
            $Arguments, (Get-Location).Path, $TimeoutSeconds, "$prefix.process.log")
        return [pscustomobject]@{ Job=$job; Process=$job.Root; Name=$Name; NativeLog=$nativeLog; Prefix=$prefix; Completed=$false }
    } catch {
        Write-Lifecycle "END $Name status=START_FAILED reason=$($_.Exception.Message)"
        throw
    } finally { $env:STREAMBRIDGE_LIFECYCLE_LOG = $previousLog }
}
function Complete-LifecycleProcess {
    param($Operation, [int]$ExpectedExitCode = 0, [switch]$Uninstaller)
    try {
        $Operation.Job.Wait()
        $code = $Operation.Job.ExitCode
        if ($Uninstaller) {
            # The NSIS launcher can exit 0 before its temporary child exits 2.
            # Require the child's terminal record AND an empty job, not launcher exit alone.
            $log = Get-Content -LiteralPath $Operation.NativeLog -Raw -Encoding unicode
            $matches = [regex]::Matches($log, '(?m)^filetime=\d+ pid=(\d+) END uninstall code=(\d+)')
            if ($matches.Count -ne 1) { throw 'Missing or ambiguous uninstaller terminal record' }
            $childCode = $Operation.Job.ObservedExitCode([int]$matches[0].Groups[1].Value)
            if ($childCode -ne [int]$matches[0].Groups[2].Value) { throw 'Actual uninstaller exit differs from its terminal record' }
            if ($code -notin @(0, $childCode)) { throw "Uninstaller launcher exit $code differs from child exit $childCode" }
            $code = $childCode
        }
        if ($code -ne $ExpectedExitCode) { throw "Expected exit $ExpectedExitCode, got $code" }
        Write-Lifecycle ("END {0} code={1} duration={2:F3}s" -f $Operation.Name,$code,$Operation.Job.Seconds)
        return $code
    } catch {
        Write-Lifecycle ("END {0} status=FAILED duration={1:F3}s diagnostic={2} reason={3}" -f $Operation.Name,$Operation.Job.Seconds,$Operation.Prefix,$_.Exception.Message)
        throw
    } finally { $Operation.Completed = $true; $Operation.Job.Dispose() }
}
function Stop-LifecycleProcess($Operation) {
    if (-not $Operation.Completed) {
        try { $Operation.Job.Dispose() }
        finally {
            $Operation.Completed = $true
            Write-Lifecycle ("END {0} status=CLEANUP duration={1:F3}s diagnostic={2}" -f $Operation.Name,$Operation.Job.Seconds,$Operation.Prefix)
        }
    }
}
function Invoke-LifecycleProcess {
    param([string]$FilePath, [string]$Arguments = '', [string]$Name,
          [ValidateRange(1,1800)][int]$TimeoutSeconds = 120,
          [int]$ExpectedExitCode = 0, [switch]$Uninstaller)
    $operation = Start-LifecycleProcess -FilePath $FilePath -Arguments $Arguments -Name $Name -TimeoutSeconds $TimeoutSeconds
    Complete-LifecycleProcess $operation -ExpectedExitCode $ExpectedExitCode -Uninstaller:$Uninstaller
}
