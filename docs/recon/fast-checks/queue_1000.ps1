# A thousand jobs of one short scenario through one queue on 8 engine processes, started as Invoke-ScenarioQueue starts them
# (docs/fast-checks.md, SP-2, the forced exit): every job has to end with a result, every process log has to hold the queue's
# "queue empty", no log a line of Zen, and every run the same two hashes. The queue and its logs are kept in
# Saved\Check\fastchecks_queue_<Tag>\queue. Changes no file of the repository. Call it in-process:
#   & docs\recon\fast-checks\queue_1000.ps1 -Tag forced
param([string]$Name = 'drop_passive', [int]$Jobs = 1000, [int]$Parallel = 8, [string]$Tag = 'q')
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
. (Join-Path $root 'Tools\common.ps1')
$work = Join-Path $RepoRoot "Saved\Check\fastchecks_queue_$Tag"
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
$file = Join-Path $RepoRoot "Scenarios\$Name.json"
$list = @(for ($i = 0; $i -lt $Jobs; $i++) { @{ File = $file; Out = (Join-Path $work ('runs\{0:D4}' -f $i)) } })
$started = Get-Date
$queue = New-ScenarioQueue -Jobs $list
$processes = @()
for ($i = 0; $i -lt $Parallel; $i++) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = Get-EditorCmd
    $info.Arguments = "`"$ProjectFile`" $(Get-QueueArguments $queue) -nullrhi -nohmd -DisablePlugins=OpenXR -unattended -nopause -nosplash -nosound -abslog=`"$(Join-Path $queue "process_$i.log")`""
    $info.WorkingDirectory = $RepoRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    if (Get-Command Set-FastStartEnvironment -ErrorAction SilentlyContinue) { Set-FastStartEnvironment $info }
    $processes += [System.Diagnostics.Process]::Start($info)
}
foreach ($p in $processes) { $p.WaitForExit() }
$wall = ((Get-Date) - $started).TotalSeconds
$hashes = @{}
$missing = 0
foreach ($job in $list) {
    $m = Read-JsonFile (Join-Path $job.Out 'metrics.json')
    if (-not $m) { $missing++; continue }
    $key = "$($m.hashes.bodies) $($m.hashes.bones)"
    if (-not $hashes.ContainsKey($key)) { $hashes[$key] = 0 }
    $hashes[$key]++
}
$withEmpty = 0; $withZen = 0; $forced = 0; $closed = 0
for ($i = 0; $i -lt $Parallel; $i++) {
    $log = Join-Path $queue "process_$i.log"
    if (Select-String -Path $log -SimpleMatch 'Scenario batch: queue empty' -Quiet) { $withEmpty++ }
    if (Select-String -Path $log -SimpleMatch 'LogZenServiceInstance' -Quiet) { $withZen++ }
    if (Select-String -Path $log -SimpleMatch 'exiting at once' -Quiet) { $forced++ }
    if (Select-String -Path $log -SimpleMatch 'Log file closed' -Quiet) { $closed++ }
}
New-Item -ItemType Directory -Force $work | Out-Null
Move-Item $queue (Join-Path $work 'queue')
'{0}: {1} jobs of {2} on {3} processes in {4:N1} s; {5} without a result; process logs with "queue empty" {6} of {3}, with Zen {7}, exited at once {8}, closed in order {9}; hashes: {10}' -f `
    $Tag, $Jobs, $Name, $Parallel, $wall, $missing, $withEmpty, $withZen, $forced, $closed, (($hashes.GetEnumerator() | ForEach-Object { "$($_.Key) x$($_.Value)" }) -join '; ')
