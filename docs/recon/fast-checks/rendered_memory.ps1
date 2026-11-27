# How much memory one engine process takes, rendered and not (docs/fast-checks.md, SP-1: how many rendered processes may run
# side by side). Runs Tools\scenario.ps1 with -Shot (rendered offscreen, as Tools\film.ps1 renders) and without, and samples the
# engine process's working set and private bytes every 100 ms; prints the peaks. Changes no file of the repository; the runs
# go to Saved\Check\fastchecks_memory_<Tag>. Call it in-process:
#   & docs\recon\fast-checks\rendered_memory.ps1 -Tag m
param([string]$Name = 'hit_chest_200_falls', [string]$Tag = 'm', [string]$ShotAt = '3.1,3.2,3.3,3.4,3.5,3.7,4.0,5.0')
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
. (Join-Path $root 'Tools\common.ps1')
foreach ($mode in @('rendered', 'headless')) {
    $out = Join-Path $RepoRoot "Saved\Check\fastchecks_memory_${Tag}_$mode"
    $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $RepoRoot 'Tools\scenario.ps1')`" -Name $Name -Out `"$out`" -Quiet"
    if ($mode -eq 'rendered') { $arguments += " -Shot -ShotAt `"$ShotAt`"" }
    $freeBefore = Get-FreeMemoryGB
    $runner = Start-Process powershell -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $peakWs = 0; $peakPrivate = 0; $leastFree = $freeBefore
    while (-not $runner.HasExited) {
        foreach ($p in @(Get-Process -Name 'UnrealEditor-Cmd' -ErrorAction SilentlyContinue)) {
            try { $p.Refresh(); if ($p.WorkingSet64 -gt $peakWs) { $peakWs = $p.WorkingSet64 }; if ($p.PrivateMemorySize64 -gt $peakPrivate) { $peakPrivate = $p.PrivateMemorySize64 } } catch {}
        }
        $free = Get-FreeMemoryGB
        if ($free -lt $leastFree) { $leastFree = $free }
        Start-Sleep -Milliseconds 100
    }
    $metrics = Read-JsonFile (Join-Path $out 'metrics.json')
    '{0} {1}: peak working set {2:N2} GB, peak private bytes {3:N2} GB, the machine''s free memory {4:N1} GB before and {5:N1} GB at least; hashes {6} {7}' -f `
        $Tag, $mode, ($peakWs / 1GB), ($peakPrivate / 1GB), $freeBefore, $leastFree, $metrics.hashes.bodies, $metrics.hashes.bones
}
