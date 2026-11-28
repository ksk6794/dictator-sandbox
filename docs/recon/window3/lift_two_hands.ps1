# Decision 46 (FR-47, "lift it a little, it must be felt as an effort"), measured as the owner decided it: two hands at 300, 400 and
# 450 N on the chest of a standing and of a lying body; the feet's load, the rise, the hands' lag, the grip's slips and tears; then R2's
# hold (the stand held by two hands, decision 13) and the drags (decision 18) at the same caps. Nothing in the files changes: the cap is
# set by -Tune / -Sets (effectors.hand.max_force_n).
#   powershell -ExecutionPolicy Bypass -File docs\recon\window3\lift_two_hands.ps1
param([string]$Caps = '300,400,450', [switch]$SkipStands)
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$out = Join-Path $repo 'Saved\Recon\window3\lift'
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force $out | Out-Null
$body = (Get-Content (Join-Path $repo 'Scenarios\grab_pull_standing.json') -Raw | ConvertFrom-Json).body

# Standing: both hands come to the chest's front at 1 m/s, close while pressing, pull straight up 40 cm over 1 s, hold 3 s, let go.
function New-HandPath([double]$X, [double]$Y0, [double]$Y1, [double]$Z, [double]$Up) {
    # (The comma binds before the plus in PowerShell: the raised height is worked out first.)
    $high = $Z + $Up
    @(
        [ordered]@{ t = 0.0; location_cm = @($X, $Y0, $Z); grip = 0 },
        [ordered]@{ t = 0.6; location_cm = @($X, $Y1, $Z); grip = 0 },
        [ordered]@{ t = 0.7; location_cm = @($X, $Y1, $Z); grip = 1 },
        [ordered]@{ t = 1.7; location_cm = @($X, $Y1, $high) },
        [ordered]@{ t = 4.7; location_cm = @($X, $Y1, $high); grip = 1 },
        [ordered]@{ t = 4.8; location_cm = @($X, $Y1, $high); grip = 0 },
        [ordered]@{ t = 5.3; location_cm = @($X, $Y0, $high); grip = 0 }
    )
}
$standing = [ordered]@{
    name = 'lift_two_hands_standing'; duration_s = 12.0; body = $body
    camera = [ordered]@{ location_cm = @(180, 160, 140); look_at_cm = @(0, 0, 110) }
    events = @(
        [ordered]@{ t = 0.0; type = 'console'; command = 'Sandbox.Tune effectors.hand.grab=1' },
        [ordered]@{ t = 3.0; type = 'effector'; effector = 'hand_left'; path = New-HandPath -10 60 4 125 40 },
        [ordered]@{ t = 3.0; type = 'effector'; effector = 'hand_right'; path = New-HandPath 10 60 4 125 40 }
    )
    expect = [ordered]@{ max_joint_stretch_cm = 3.0 }
}
$standing | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $out 'lift_two_hands_standing.json') -Encoding utf8

# Lying: felled as grab_drag_fallen_two fells it, both hands take hold where that scenario's do, and pull straight up 50 cm.
$fallen = Get-Content (Join-Path $repo 'Scenarios\grab_drag_fallen_two.json') -Raw | ConvertFrom-Json
$lying = [ordered]@{
    name = 'lift_two_hands_lying'; duration_s = 16.0; body = $fallen.body
    camera = [ordered]@{ location_cm = @(200, -60, 120); look_at_cm = @(0, -180, 30) }
    events = @(
        [ordered]@{ t = 0.0; type = 'console'; command = 'Sandbox.Tune effectors.hand.grab=1' },
        $fallen.events[1],
        [ordered]@{ t = 7.0; type = 'effector'; effector = 'hand_left'; path = @(
            [ordered]@{ t = 0.0; location_cm = @(-14, -177, 90); grip = 0 },
            [ordered]@{ t = 0.6; location_cm = @(-14, -177, 27); grip = 0 },
            [ordered]@{ t = 1.0; location_cm = @(-14, -177, 27); grip = 0 },
            [ordered]@{ t = 1.1; location_cm = @(-14, -177, 27); grip = 1 },
            [ordered]@{ t = 2.1; location_cm = @(-14, -177, 77) },
            [ordered]@{ t = 5.1; location_cm = @(-14, -177, 77); grip = 1 },
            [ordered]@{ t = 5.2; location_cm = @(-14, -177, 77); grip = 0 },
            [ordered]@{ t = 5.7; location_cm = @(-14, -177, 110); grip = 0 }) },
        [ordered]@{ t = 7.0; type = 'effector'; effector = 'hand_right'; path = @(
            [ordered]@{ t = 0.0; location_cm = @(6, -177, 90); grip = 0 },
            [ordered]@{ t = 0.6; location_cm = @(6, -177, 27); grip = 0 },
            [ordered]@{ t = 1.0; location_cm = @(6, -177, 27); grip = 0 },
            [ordered]@{ t = 1.1; location_cm = @(6, -177, 27); grip = 1 },
            [ordered]@{ t = 2.1; location_cm = @(6, -177, 77) },
            [ordered]@{ t = 5.1; location_cm = @(6, -177, 77); grip = 1 },
            [ordered]@{ t = 5.2; location_cm = @(6, -177, 77); grip = 0 },
            [ordered]@{ t = 5.7; location_cm = @(6, -177, 110); grip = 0 }) }
    )
    expect = [ordered]@{ max_joint_stretch_cm = 3.0 }
}
$lying | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $out 'lift_two_hands_lying.json') -Encoding utf8

$sets = ($Caps -split ',' | ForEach-Object { "effectors.hand.max_force_n=$_" }) -join '|'
& powershell -ExecutionPolicy Bypass -File Tools\sweep.ps1 -Name w3_lift -Files $out -Sets $sets -Metrics 'control.end_state' *> (Join-Path $repo 'Saved\Recon\window3\lift_sweep.txt')
Get-Content (Join-Path $repo 'Saved\Recon\window3\lift_sweep.txt') | Select-Object -First 1
$capList = @($Caps -split ',')
$table = foreach ($m in Get-ChildItem (Join-Path $repo 'Saved\Sweeps\w3_lift') -Recurse -Filter metrics.json | Sort-Object FullName) {
    $j = Get-Content $m.FullName -Raw | ConvertFrom-Json
    $set = [int]$m.Directory.Parent.Name
    $series = Import-Csv (Join-Path $m.DirectoryName 'series.csv')
    $lying = $m.Directory.Name -match 'lying'
    $from = if ($lying) { 8.1 } else { 3.7 }
    $before = @($series | Where-Object { [double]$_.t -ge $from - 0.3 -and [double]$_.t -lt $from })
    $during = @($series | Where-Object { [double]$_.t -ge $from + 1.0 -and [double]$_.t -le $from + 4.0 })
    $pe0 = ($before | ForEach-Object { [double]$_.potential_j } | Measure-Object -Average).Average
    $peMax = ($during | ForEach-Object { [double]$_.potential_j } | Measure-Object -Maximum).Maximum
    $feet = $during | ForEach-Object { [double]$_.foot_left_n + [double]$_.foot_right_n }
    $l = $j.effectors.list.hand_left; $r = $j.effectors.list.hand_right
    [pscustomobject]@{
        scenario = $m.Directory.Name; cap_n = $capList[$set - 1]
        rise_cm = [Math]::Round(($peMax - $pe0) / (80.0 * 9.81) * 100.0, 1)
        feet_min_n = [Math]::Round((($feet | Measure-Object -Minimum).Minimum), 0)
        feet_mean_n = [Math]::Round((($feet | Measure-Object -Average).Average), 0)
        grabs = "$($l.grabs)/$($r.grabs)"; slips = [int]$l.grab_slips + [int]$r.grab_slips; tears = [int]$l.grab_overloads + [int]$r.grab_overloads
        gap_max_cm = [Math]::Round([Math]::Max([double]$l.max_gap_cm, [double]$r.max_gap_cm), 1)
        grab_force_max_n = [Math]::Round([Math]::Max([double]$l.max_grab_force_n, [double]$r.max_grab_force_n), 0)
        states = $j.control.state_sequence; end = $j.control.end_state
    }
}
$table | Format-Table -AutoSize | Out-String -Width 220
$table | Export-Csv (Join-Path $repo 'Saved\Recon\window3\lift.csv') -NoTypeInformation
if (-not $SkipStands) {
    foreach ($cap in $capList) {
        & powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label "w3_hold2_$cap" -HoldHands 2 -Tune "effectors.hand.max_force_n=$cap" *> (Join-Path $repo "Saved\Recon\window3\lift_hold2_$cap.txt")
        Write-Output ("hold by two at {0} N: {1}" -f $cap, (Get-Content (Join-Path $repo "Saved\Recon\window3\lift_hold2_$cap.txt") | Select-Object -First 1))
    }
    $drags = (Get-ChildItem (Join-Path $repo 'Scenarios') -Filter 'getup_drag_*.json' | ForEach-Object { $_.BaseName }) -join ','
    & powershell -ExecutionPolicy Bypass -File Tools\sweep.ps1 -Name w3_lift_drags -Scenarios $drags -Sets $sets -Metrics 'control.end_state' *> (Join-Path $repo 'Saved\Recon\window3\lift_drags.txt')
    Get-Content (Join-Path $repo 'Saved\Recon\window3\lift_drags.txt') | Select-Object -First 1
}
'LIFT DONE'
