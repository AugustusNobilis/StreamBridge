$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lifecycle-process.ps1"
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
Initialize-Lifecycle (Join-Path $workspace ('build/lifecycle-diagnostics/roles-' + [Guid]::NewGuid().ToString('N')))
$fixture = $script:LifecycleDirectory
$watch = [Diagnostics.Stopwatch]::StartNew()
# Small real executables, no shell delays: exercise creation/exit event capture.
$source = Join-Path $fixture 'fixture.cs'
@'
using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
class Fixture {
    static void Start(string name, string arguments) {
        var p = Process.Start(new ProcessStartInfo(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, name), arguments) {
            UseShellExecute=false, CreateNoWindow=true, WindowStyle=ProcessWindowStyle.Hidden
        });
        p.Dispose(); // Intentionally neither wait nor relay the child's code.
    }
    static int Main(string[] args) {
        switch (args[0]) {
        case "hang": while (true) Thread.Sleep(100);
        case "child":
            if (args.Length > 2) Thread.Sleep(int.Parse(args[2]));
            return int.Parse(args[1]);
        case "parent": Start("setup.exe", "child " + args[1] + " " + args[2]); return 0;
        case "chain": Start("helper.exe", "helper " + args[1]); return 0;
        case "helper": Start("setup.exe", "child " + args[1]); return 0;
        case "helper-fail": Start("helper.exe", "child 9"); return 0;
        case "child-hang": Start("setup.exe", "hang"); return 0;
        default: return 0;
        }
    }
}
'@ | Set-Content $source -Encoding utf8
$compiler = "$env:WINDIR/Microsoft.NET/Framework64/v4.0.30319/csc.exe"
$parent = Join-Path $fixture 'parent.exe'
$null = Invoke-LifecycleProcess $compiler ('/nologo /target:exe /out:"{0}" "{1}"' -f $parent,$source) compile 30
Copy-Item $parent "$fixture/setup.exe"
Copy-Item $parent "$fixture/helper.exe"
function Role([string]$Name,[int]$Count=1,[int]$Code=0) {
    $file = if ($Name -eq 'installer') { 'setup.exe' } else { "$Name.exe" }
    return @{ Name=$Name; Path="$fixture/$file"; Count=$Count; ExitCode=$Code }
}
function Check([string]$Name,[string]$Arguments,[object[]]$Roles,[string]$ExpectedFailure='',[int]$Timeout=10) {
    $operation = Start-LifecycleProcess $parent $Arguments $Name $Timeout
    $operation.Roles = $Roles
    $caught = $null
    try { $null = Complete-LifecycleProcess $operation }
    catch { $caught = $_ }
    finally { Stop-LifecycleProcess $operation }
    if ($ExpectedFailure) {
        if (-not $caught -or $caught.Exception.Message -notmatch $ExpectedFailure) { throw "Expected failure '$ExpectedFailure', got '$caught'" }
        Write-Lifecycle "PASS $Name detected expected operation error: $($caught.Exception.Message)"
    } else {
        if ($caught) { throw $caught }
        Write-Lifecycle "PASS $Name actual role exit codes verified"
    }
    return $operation
}
$sentinel = Start-LifecycleProcess $parent hang unrelated-sentinel 60
try {
    $null = Check parent0-installer7 'parent 7 0' @((Role installer)) 'Role installer expected exit 0, got 7 \(parent=0\)'
    $null = Check parent0-helper0-installer7 'chain 7' @((Role helper),(Role installer)) 'Role installer expected exit 0, got 7 \(parent=0\)'
    $null = Check helper-failure 'helper-fail' @((Role helper),(Role installer 0)) 'Role helper expected exit 0, got 9'
    $null = Check success 'chain 0' @((Role helper),(Role installer))
    $null = Check absent-installer none @((Role installer)) 'Role installer expected 1 process\(es\), observed 0'
    $null = Check child-after-parent 'parent 0 400' @((Role installer))
    # Repeat genuinely fast child exits to exercise the old polling blind spot.
    1..10 | ForEach-Object { $null = Check "fast-child-$_" 'parent 0 0' @((Role installer)) }
    $null = Check hung-child child-hang @((Role installer)) 'timed out' 2
    if ($sentinel.Process.HasExited) { throw 'Unrelated sentinel was killed' }
    Write-Lifecycle 'PASS unrelated process preserved after failing roles and timeout'
    $operation = Start-LifecycleProcess $parent none missing-os-exit 10
    # A terminal log alone is not evidence: this PID has no held process handle.
    Set-Content $operation.NativeLog 'filetime=1 pid=2147483647 END uninstall code=0' -Encoding unicode
    $caught = $false
    try { $null = Complete-LifecycleProcess $operation -Uninstaller }
    catch {
        if ($_.Exception.Message -notmatch 'No completed process handle') { throw }
        $caught = $true
    } finally { Stop-LifecycleProcess $operation }
    if (-not $caught) { throw 'Missing OS exit code was accepted' }
    Write-Lifecycle 'PASS missing actual OS exit is rejected despite a success record'
} finally { Stop-LifecycleProcess $sentinel }
Write-Lifecycle ('SUITE END lifecycle-roles status=PASS duration={0:F3}s' -f $watch.Elapsed.TotalSeconds)
