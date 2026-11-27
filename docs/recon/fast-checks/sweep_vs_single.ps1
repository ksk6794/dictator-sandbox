# SP-1's acceptance (docs/fast-checks.md): the table's 4 scenarios times 3 sets in one call of Tools\sweep.ps1 -Sets, timed, and
# every run of it held against the same scenario with the same values run alone by Tools\scenario.ps1, by both hashes. The single
# runs are this check's own loop over scenario.ps1, made once to hold the sweep against them. Changes no file of the repository;
# runs in Saved\Sweeps\fastchecks_sets_<Tag> and Saved\Check\fastchecks_single_<Tag>. Call it in-process:
#   & docs\recon\fast-checks\sweep_vs_single.ps1 -Tag s1
param([string]$Tag = 's', [string]$Scenarios = 'getup_back,getup_front,getup_left,getup_right', [string]$Sets = 'getup.start_blend_s=0.3|getup.start_blend_s=0.6|getup.start_blend_s=0')
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
. (Join-Path $root 'Tools\common.ps1')
$sweepName = "fastchecks_sets_$Tag"
$started = Get-Date
$printed = @(& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $RepoRoot 'Tools\sweep.ps1') -Name $sweepName -Scenarios $Scenarios -Sets $Sets)
$sweepSeconds = ((Get-Date) - $started).TotalSeconds
'{0}: the sweep took {1:N1} s; {2}' -f $Tag, $sweepSeconds, $printed[0]
$sweep = Read-JsonFile (Join-Path $RepoRoot "Saved\Sweeps\$sweepName\sweep.json")
$singleRoot = Join-Path $RepoRoot "Saved\Check\fastchecks_single_$Tag"
if (Test-Path $singleRoot) { Remove-Item $singleRoot -Recurse -Force }
$setList = @($Sets.Split('|'))
$same = 0; $differ = @()
foreach ($row in $sweep.rows) {
    $tune = $setList[[int]$row.setting - 1]
    $out = Join-Path $singleRoot ('{0:000}\{1}' -f [int]$row.setting, $row.scenario)
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $RepoRoot 'Tools\scenario.ps1'), '-Name', $row.scenario, '-Out', $out, '-Quiet')
    if ($tune) { $arguments += @('-Tune', $tune) }
    $null = & powershell @arguments
    $alone = Read-JsonFile (Join-Path $out 'metrics.json')
    $inSweep = Read-JsonFile (Join-Path $RepoRoot ('Saved\Sweeps\{0}\runs\{1:000}\{2}\metrics.json' -f $sweepName, [int]$row.setting, $row.scenario))
    if ($alone -and $inSweep -and "$($alone.hashes.bodies) $($alone.hashes.bones)" -eq "$($inSweep.hashes.bodies) $($inSweep.hashes.bones)") { $same++ }
    else { $differ += ('{0} [{1}]: sweep {2} {3}, alone {4} {5}' -f $row.scenario, $tune, $inSweep.hashes.bodies, $inSweep.hashes.bones, $alone.hashes.bodies, $alone.hashes.bones) }
}
'{0}: {1} of {2} runs of the sweep have the hashes of the same run alone' -f $Tag, $same, @($sweep.rows).Count
$differ | ForEach-Object { '  differs: ' + $_ }
