# The suite's jobs (every scenario of 60 s or less) through one queue on 8 processes, started as Invoke-ScenarioQueue starts them,
# with extra engine arguments, into Saved\Check\fastchecks_suite_<Tag> (docs/fast-checks.md, SP-2). Then every run's hashes
# against the latest suite (Saved\Scenarios), the peak memory, and each process's start and exit from its log. Changes no file of
# the repository and leaves Saved\Scenarios and Saved\Tools\suite.json as they are. Call it in-process:
#   & docs\recon\fast-checks\suite_run.ps1 -Tag nozen -Extra '-ddc=InstalledNoZenLocalFallback'
param([string]$Extra = '', [string]$Tag = 's', [int]$Parallel = 8)
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
. (Join-Path $root 'Tools\common.ps1')
$runRoot = Join-Path $RepoRoot "Saved\Check\fastchecks_suite_$Tag"
if (Test-Path $runRoot) { Remove-Item $runRoot -Recurse -Force }
$files = @(Get-ChildItem (Join-Path $RepoRoot 'Scenarios') -Filter '*.json' | Where-Object { [double](Read-JsonFile $_.FullName).duration_s -le 60 } | Sort-Object Name)
$jobs = @($files | ForEach-Object { @{ Name = $_.BaseName; Out = (Join-Path $runRoot $_.BaseName) } })
$started = Get-Date
$queue = New-ScenarioQueue -Jobs $jobs
$processes = @()
for ($i = 0; $i -lt $Parallel; $i++) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = Get-EditorCmd
    $info.Arguments = "`"$ProjectFile`" $(Get-QueueArguments $queue) -nullrhi -nohmd -DisablePlugins=OpenXR -unattended -nopause -nosplash -nosound -abslog=`"$(Join-Path $queue "process_$i.log")`" $Extra"
    $info.WorkingDirectory = $RepoRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $processes += [System.Diagnostics.Process]::Start($info)
}
$peakMb = 0
while (@($processes | Where-Object { -not $_.HasExited }).Count -gt 0) {
    $mb = 0
    foreach ($p in $processes) { if (-not $p.HasExited) { try { $p.Refresh(); $mb += $p.WorkingSet64 / 1MB } catch {} } }
    if ($mb -gt $peakMb) { $peakMb = $mb }
    Start-Sleep -Milliseconds 500
}
$wall = ((Get-Date) - $started).TotalSeconds
$same = 0; $differ = @(); $missing = @()
foreach ($job in $jobs) {
    $new = Read-JsonFile (Join-Path $job.Out 'metrics.json')
    $old = Read-JsonFile (Join-Path $RepoRoot "Saved\Scenarios\$($job.Name)\metrics.json")
    if (-not $new) { $missing += $job.Name; continue }
    if ($old -and "$($old.hashes.bodies) $($old.hashes.bones)" -eq "$($new.hashes.bodies) $($new.hashes.bones)") { $same++ } else { $differ += $job.Name }
}
'{0}: {1:N1} s wall for {2} jobs on {3} processes; peak working set {4:N1} GB; same hashes {5}, other {6}, no result {7}' -f $Tag, $wall, $jobs.Count, $Parallel, ($peakMb / 1024), $same, $differ.Count, $missing.Count
if ($differ.Count) { '  other hashes: ' + ($differ -join ', ') }
function Get-Stamp($line) {
    if ($line -match '^\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):(\d{3})\]') { return (Get-Date -Year $Matches[1] -Month $Matches[2] -Day $Matches[3] -Hour $Matches[4] -Minute $Matches[5] -Second $Matches[6] -Millisecond $Matches[7]) }
    return $null
}
$rows = @()
for ($i = 0; $i -lt $Parallel; $i++) {
    $lines = Get-Content (Join-Path $queue "process_$i.log")
    $stamped = @($lines | Where-Object { $_ -match '^\[\d{4}\.' })
    $t0 = Get-Stamp $stamped[0]
    $zenS = 0.0
    $zen = $lines | Where-Object { $_ -match 'AutoLaunch initialization completed in ([\d.]+)' } | Select-Object -First 1
    if ($zen -match 'completed in ([\d.]+)') { $zenS = [double]$Matches[1] }
    $ubt = Get-Stamp ($lines | Where-Object { $_ -match 'UBT AutoSDK ReturnCode' } | Select-Object -First 1)
    $first = Get-Stamp ($lines | Where-Object { $_ -match 'Scenario batch: job 1,' } | Select-Object -First 1)
    $empty = Get-Stamp ($lines | Where-Object { $_ -match 'Scenario batch: queue empty' } | Select-Object -First 1)
    $ubtS = $null; if ($ubt) { $ubtS = [Math]::Round(($ubt - $t0).TotalSeconds, 2) }
    $rows += [pscustomobject]@{ process = $i; ubt_returned_s = $ubtS; zen_s = $zenS; start_to_job1_s = [Math]::Round(($first - $t0).TotalSeconds, 2)
        jobs = @($lines | Where-Object { $_ -match 'Scenario batch: job \d+ done' }).Count; working_s = [Math]::Round(($empty - $first).TotalSeconds, 1); exit_s = [Math]::Round(((Get-Stamp $stamped[-1]) - $empty).TotalSeconds, 2) }
}
$rows | Format-Table -AutoSize | Out-String
'mean start to job 1: {0:N2} s, mean exit {1:N2} s; the queue and its logs are kept in {2}' -f (($rows | Measure-Object start_to_job1_s -Average).Average), (($rows | Measure-Object exit_s -Average).Average), $queue
