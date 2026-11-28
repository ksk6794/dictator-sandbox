# The sweep table (C30, decision 43; docs/PRD.md, M4's rest: "a table of kicks at the shin, the calf and the knee from four sides at
# three speeds, its outcomes laid beside the owner's table"), measured on today's body: the desktop's foot (effectors.foot: 10 kg,
# 3500 N) kicks the standing body's right leg at the lower leg (30 cm up) and the knee (50 cm) from the front, the back, the outside and
# the front-outside, at 4, 6 and 8 m/s, through the leg's middle by 6 cm and back, as strike_kick_calf does. A row a kick: what the foot
# delivered, what the body did (stood, stepped, fell). Scenario files of its own (not the suite); nothing in the files changes.
#   powershell -ExecutionPolicy Bypass -File docs\recon\window3\sweep_table.ps1
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$out = Join-Path $repo 'Saved\Recon\window3\sweep_table'
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force $out | Out-Null
$base = Get-Content (Join-Path $repo 'Scenarios\strike_kick_calf.json') -Raw | ConvertFrom-Json
# The right leg's axis over the floor, as strike_kick_calf aims at it.
$legX = 12.0
$legY = 0.0
$sides = [ordered]@{ front = @(0.0, 1.0); back = @(0.0, -1.0); outside = @(1.0, 0.0); front_outside = @(0.7071, 0.7071) }
$heights = [ordered]@{ lower_leg = 30.0; knee = 50.0 }
foreach ($side in $sides.Keys) {
    foreach ($height in $heights.Keys) {
        foreach ($mps in @(4, 6, 8)) {
            $d = $sides[$side]
            $z = $heights[$height]
            $startX = $legX + $d[0] * 100.0
            $startY = $legY + $d[1] * 100.0
            $endX = $legX - $d[0] * 6.0
            $endY = $legY - $d[1] * 6.0
            $time = [Math]::Round(1.06 / $mps, 4)
            $s = $base | ConvertTo-Json -Depth 20 | ConvertFrom-Json
            $name = "sweep_${side}_${height}_$mps"
            $s.name = $name
            $s.duration_s = 10.0
            $startZ = $z - 10.0
            $s.events = @([ordered]@{ t = 3.0; type = 'effector'; effector = 'foot_right'; path = @(
                [ordered]@{ t = 0.0; location_cm = @($startX, $startY, $startZ) },
                [ordered]@{ t = $time; location_cm = @($endX, $endY, $z) },
                [ordered]@{ t = $time + 0.4; location_cm = @($startX, $startY, $startZ) }) })
            $s.expect = [ordered]@{ max_joint_stretch_cm = 3.0 }
            $s | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $out "$name.json") -Encoding utf8
        }
    }
}
& powershell -ExecutionPolicy Bypass -File Tools\sweep.ps1 -Name w3_sweep_table -Files $out -Sets 'effectors.foot.max_force_n=3500' -Metrics 'control.end_state' *> (Join-Path $repo 'Saved\Recon\window3\sweep_table_sweep.txt')
Get-Content (Join-Path $repo 'Saved\Recon\window3\sweep_table_sweep.txt') | Select-Object -First 1
$table = foreach ($m in Get-ChildItem (Join-Path $repo 'Saved\Sweeps\w3_sweep_table') -Recurse -Filter metrics.json | Sort-Object { $_.Directory.Name }) {
    $j = Get-Content $m.FullName -Raw | ConvertFrom-Json
    $foot = $j.effectors.list.foot_right
    $outcome = if ($j.metrics.fell) { 'falls' } elseif ([int]$j.control.step_count -gt 0) { "steps ($($j.control.step_count))" } else { 'stands' }
    [pscustomobject]@{
        kick = $m.Directory.Name -replace '^sweep_', ''
        arrived_mps = [Math]::Round([double]$foot.first_contact_speed_mps, 1)
        delivered_ns = [Math]::Round([double]$foot.contact_impulse_ns, 1)
        struck = $foot.struck_bone
        outcome = $outcome
        states = ($j.control.state_sequence -join '>')
    }
}
$table | Format-Table -AutoSize | Out-String -Width 200
$table | Export-Csv (Join-Path $repo 'Saved\Recon\window3\sweep_table.csv') -NoTypeInformation
'SWEEP TABLE DONE'
