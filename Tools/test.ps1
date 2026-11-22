# Runs the project's automation tests headless.
#   powershell -ExecutionPolicy Bypass -File Tools\test.ps1 [-Filter DictatorSandbox]
# Prints a summary with every failed test and writes Saved\Tools\test.json.
# Exit code: 0 all passed, 1 failures (or no test matched the filter), 2 the run itself broke.
param(
    [string]$Filter = 'DictatorSandbox',
    [int]$TimeoutSec = 900
)
. "$PSScriptRoot\common.ps1"

$reportDir = Join-Path $ToolsOut 'test-report'
$logFile = Join-Path $ToolsOut 'test.log'
$resultFile = Join-Path $ToolsOut 'test.json'
if (Test-Path $reportDir) { Remove-Item $reportDir -Recurse -Force }

$arguments = "-ExecCmds=`"Automation RunTests $Filter;Quit`" -testexit=`"Automation Test Queue Empty`" -ReportExportPath=`"$reportDir`" -nullrhi"
$run = Invoke-Editor -ArgumentString $arguments -LogFile $logFile -TimeoutSec $TimeoutSec

$report = Read-JsonFile (Join-Path $reportDir 'index.json')
if ($run.TimedOut -or -not $report) {
    Write-JsonFile $resultFile ([ordered]@{ ok = $false; error = 'no test report'; timed_out = $run.TimedOut; exit_code = $run.ExitCode; log = $logFile })
    Write-Output "TESTS ERROR: no report was produced (exit code $($run.ExitCode), timed out: $($run.TimedOut)). Log: $logFile"
    Get-LogErrors $logFile | ForEach-Object { Write-Output "  $_" }
    exit 2
}

$tests = @($report.tests)
$failed = @($tests | Where-Object { $_.state -ne 'Success' })
$total = $tests.Count
$ok = ($total -gt 0) -and ($failed.Count -eq 0)

Write-JsonFile $resultFile ([ordered]@{
    ok        = $ok
    filter    = $Filter
    total     = $total
    passed    = $total - $failed.Count
    failed    = @($failed | ForEach-Object { [ordered]@{ test = $_.fullTestPath; state = $_.state; messages = @($_.entries | ForEach-Object { $_.event.message }) } })
    report    = (Join-Path $reportDir 'index.json')
    log       = $logFile
})

if ($total -eq 0) {
    Write-Output "TESTS FAILED: filter '$Filter' matched no tests"
    exit 1
}
if ($ok) {
    Write-Output "TESTS OK: $total passed (filter '$Filter')"
    exit 0
}
Write-Output "TESTS FAILED: $($failed.Count) of $total failed (filter '$Filter')"
foreach ($test in $failed) {
    Write-Output "  [$($test.state)] $($test.fullTestPath)"
    $test.entries | Where-Object { $_.event.type -ne 'Info' } | Select-Object -First 5 | ForEach-Object { Write-Output "      $($_.event.message)" }
}
exit 1
