# The unit tests as Tools\test.ps1 runs them, with another automation command and extra engine arguments, into
# Saved\Check\fastchecks_<Tag> (docs/fast-checks.md, SP-2). Changes no file of the repository; the editor writes its asset
# registry cache, as test.ps1 makes it do. Call it in-process:
#   & docs\recon\fast-checks\test_run.ps1 -Tag now -Commands 'Now;RunTests DictatorSandbox;Quit'
#   & docs\recon\fast-checks\test_run.ps1 -Tag wrong -Commands 'RunTests Now DictatorSandbox;Quit'   (runs no test, exit code 0)
param([string]$Commands = 'RunTests DictatorSandbox;Quit', [string]$Extra = '', [string]$Tag = 't')
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
. (Join-Path $root 'Tools\common.ps1')
$dir = Join-Path $RepoRoot "Saved\Check\fastchecks_$Tag"
if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
$report = Join-Path $dir 'report'
$log = Join-Path $dir 'test.log'
$started = Get-Date
$run = Invoke-Editor -ArgumentString "-ExecCmds=`"Automation $Commands`" -testexit=`"Automation Test Queue Empty`" -ReportExportPath=`"$report`" -nullrhi $Extra" -LogFile $log -TimeoutSec 300
$seconds = ((Get-Date) - $started).TotalSeconds
$index = Read-JsonFile (Join-Path $report 'index.json')
$count = 0; $passed = 0
if ($index) { $count = @($index.tests).Count; $passed = @($index.tests | Where-Object { $_.state -eq 'Success' }).Count }
'{0}: {1:N1} s wall, exit {2}, tests {3}, passed {4}' -f $Tag, $seconds, $run.ExitCode, $count, $passed
Select-String -Path $log -Pattern 'Ready to start automation|FindWorkersMessage|automation tests based on|TEST COMPLETE|AutoLaunch initialization|Asset registry cache written' |
    ForEach-Object { '  ' + $_.Line.Substring(0, [Math]::Min(150, $_.Line.Length)) }
