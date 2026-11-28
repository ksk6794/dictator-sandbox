# Stage 2 of the second window of 28 November (decision 58 under the owner's delegation): R3's variant A measured on the body with
# R10 on. One sweep of the 96 falls of R3's larger set (24 directions of a blow of 160 and 240 N*s, 24 shoves by hand at 3.5 and
# 4.5 m/s; the files R3 generated, Saved\Recon\r3\big) as the files have them and with A; then one check set with A (the suite, the
# matrix, the knockdowns, the stand on three seeds, the scorecard); then the held and the pressed stands with A.
#   powershell -ExecutionPolicy Bypass -File docs\recon\night2\r3_gate.ps1
$ErrorActionPreference = 'Continue'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$A = 'fall.catch=1,fall.arm_curve=1,fall.head=1,fall.palms=1'
$falls = Join-Path $repo 'Saved\Recon\night2\falls96'
if (Test-Path $falls) { Remove-Item $falls -Recurse -Force }
New-Item -ItemType Directory -Force $falls | Out-Null
Get-ChildItem (Join-Path $repo 'Saved\Recon\r3\big') -Filter '*.json' | Where-Object { $_.Name -like 'knockdown_spine*' -or $_.Name -like 'shove*' } |
    ForEach-Object { Copy-Item $_.FullName $falls }
$log = Join-Path $repo 'Saved\Recon\night2'
Write-Output ("falls: {0}" -f (Get-ChildItem $falls -Filter '*.json').Count)
& powershell -ExecutionPolicy Bypass -File Tools\sweep.ps1 -Name n2_r3_96 -Files $falls -Sets "|$A" -Metrics 'control.fall.head_struck,control.fall.head_impact_speed_mps,control.fall.hands_ahead_first,control.fall.first_touch_bone,energy.max_gain_j,control.pose.target_step_max_deg' *> (Join-Path $log 'sweep_96.txt')
Write-Output ((Get-Content (Join-Path $log 'sweep_96.txt') | Select-Object -First 1))
& powershell -ExecutionPolicy Bypass -File Tools\checks.ps1 -What 'suite,matrix,knockdowns,seeds,natural' -Tune $A -Label n2_r3A *> (Join-Path $log 'checks_A.txt')
Write-Output ((Get-Content (Join-Path $log 'checks_A.txt') | Where-Object { $_ -match '^CHECKS|^\s+(suite|matrix|knockdowns|seeds|natural)\s' }) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\knockdowns.ps1 -ByHand -Tune $A -Out (Join-Path $repo 'Saved\Matrix\knockdowns_by_hand_n2_r3A') *> (Join-Path $log 'kdh_A.txt')
& powershell -ExecutionPolicy Bypass -File Tools\knockdowns.ps1 -ByHand -Out (Join-Path $repo 'Saved\Matrix\knockdowns_by_hand_n2_off') *> (Join-Path $log 'kdh_off.txt')
Write-Output ((Get-Content (Join-Path $log 'kdh_A.txt') | Select-Object -First 1))
Write-Output ((Get-Content (Join-Path $log 'kdh_off.txt') | Select-Object -First 1))
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_r3A_hold1 -HoldHands 1 -Tune $A *> (Join-Path $log 'seeds_A_hold1.txt')
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_r3A_press -PressN 300 -Tune $A *> (Join-Path $log 'seeds_A_press.txt')
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_r3A_near -Near 70 -Tune $A *> (Join-Path $log 'seeds_A_near.txt')
Write-Output ((Get-Content (Join-Path $log 'seeds_A_hold1.txt') | Select-Object -First 1))
Write-Output ((Get-Content (Join-Path $log 'seeds_A_press.txt') | Select-Object -First 1))
Write-Output ((Get-Content (Join-Path $log 'seeds_A_near.txt') | Select-Object -First 1))
'R3 GATE DONE'
