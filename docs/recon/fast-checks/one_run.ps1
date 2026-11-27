# One scenario run as Tools\scenario.ps1 makes it (a queue of one job, the warm-up, -nullrhi), with extra engine arguments, for
# an experiment of docs/fast-checks.md (SP-2). Changes no file of the repository; results in Saved\Check\fastchecks_<Tag>.
# Prints the wall time, the hashes and Zen's lines. Call it in-process, so that an argument that starts with '-' is not
# taken for a parameter:
#   & docs\recon\fast-checks\one_run.ps1 -Name getup_left -Tag nozen -Extra '-ddc=InstalledNoZenLocalFallback'
#   & docs\recon\fast-checks\one_run.ps1 -Tag zen_alive -Extra '-ini:Engine:[Zen.AutoLaunch]:LimitProcessLifetime=False'
# (the second leaves a Zen server running; the next run without the flag shuts it down, as it would anyone's.)
param([string]$Name = 'getup_left', [string]$Extra = '', [string]$Tag = 'x')
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
. (Join-Path $root 'Tools\common.ps1')
$out = Join-Path $RepoRoot "Saved\Check\fastchecks_$Tag"
$started = Get-Date
$queue = New-ScenarioQueue -Jobs @(@{ File = (Join-Path $RepoRoot "Scenarios\$Name.json"); Out = $out })
$log = Join-Path $out 'process.log'
$run = Invoke-Editor -ArgumentString ((Get-QueueArguments -Queue $queue -Fps 90) + ' -nullrhi ' + $Extra) -LogFile $log -TimeoutSec 300
$seconds = ((Get-Date) - $started).TotalSeconds
$metrics = Read-JsonFile (Join-Path $out 'metrics.json')
'{0}: {1:N1} s wall, exit {2}, hashes {3} {4}' -f $Tag, $seconds, $run.ExitCode, $metrics.hashes.bodies, $metrics.hashes.bones
Select-String -Path $log -Pattern 'AutoLaunch initialization|existing instance|No current process using|ValidatePlatforms|UBT AutoSDK ReturnCode|Scenario batch: job 1|queue empty' |
    ForEach-Object { '  ' + $_.Line.Substring(0, [Math]::Min(160, $_.Line.Length)) }
if (@(Get-ChildItem $queue -Filter '*.json' -File -ErrorAction SilentlyContinue).Count -eq 0) { Remove-Item $queue -Recurse -Force -ErrorAction SilentlyContinue }
