# Reads a sweep's runs of the R10 scenarios and prints, a row a run: the first cover's place and its times (C34), the lowering's speeds
# (C33), the first command's first motion (C39), the largest target step and command jump (C25), and the energy gained.
#   powershell -ExecutionPolicy Bypass -File docs\recon\night2\cover_table.ps1 -Sweep n2_cover_blend
param([Parameter(Mandatory)][string]$Sweep)
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$root = Join-Path $repo "Saved\Sweeps\$Sweep\runs"
$fmt = '{0,-4} {1,-28} {2,5} {3,-6} {4,6} {5,6} {6,6} {7,6} {8,6} {9,6} {10,7} {11,6}'
Write-Output ($fmt -f 'set', 'scenario', 'pass', 'place', 'one_s', 'both_s', 'headDn', 'lowPel', 'lowHd', 'cmd_s', 'jumpNm', 'stepDg')
foreach ($set in Get-ChildItem $root -Directory | Sort-Object Name) {
    foreach ($run in Get-ChildItem $set.FullName -Directory | Sort-Object Name) {
        $file = Join-Path $run.FullName 'metrics.json'
        if (-not (Test-Path $file)) { continue }
        $m = Get-Content $file -Raw | ConvertFrom-Json
        $b = $m.control.behaviour
        $c = @($b.covers)[0]
        $l = @($b.lowerings)[0]
        $k = @($m.control.commands)[0]
        $n = { param($v) if ($null -eq $v) { '-' } else { '{0:N2}' -f [double]$v } }
        Write-Output ($fmt -f $set.Name, $run.Name.Substring(0, [Math]::Min(28, $run.Name.Length)), $m.pass, $(if ($c) { $c.place } else { '-' }),
            (& $n $c.one_within_s), (& $n $c.both_within_s), (& $n $c.head_down_mps), (& $n $l.pelvis_down_mps), (& $n $l.head_down_mps),
            (& $n $k.first_motion_after_s), ('{0:N0}' -f $m.control.pose.command_jump_max_nm), ('{0:N0}' -f $m.control.pose.target_step_max_deg))
    }
}
