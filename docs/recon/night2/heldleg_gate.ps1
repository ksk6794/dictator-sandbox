# behaviour.protect_held_leg on, measured by R10's gate's tools (28 November): the stand on three seeds and the scorecard through one
# check set, the stand with a viewer at 70 cm, with get_up 3 s after it came, and held by one hand; each held run by run against r10on_*.
#   powershell -ExecutionPolicy Bypass -File docs\recon\night2\heldleg_gate.ps1
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$log = Join-Path $repo 'Saved\Recon\night2'
$on = 'behaviour.protect_held_leg=1'
& powershell -ExecutionPolicy Bypass -File Tools\checks.ps1 -What 'seeds,natural' -Tune $on -Label n2_heldleg1 *> (Join-Path $log 'heldleg_checks.txt')
Write-Output ((Get-Content (Join-Path $log 'heldleg_checks.txt') | Where-Object { $_ -match '^CHECKS|^\s+(seeds|natural)\s' }) -join "`n")
$seeds = Get-ChildItem (Join-Path $repo 'Saved\Checks\n2_heldleg1') -Recurse -Directory | Where-Object { $_.Name -eq 'seeds' } | Select-Object -First 1
if ($seeds) {
    & powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns $seeds.FullName -Name r10on_seeds *> (Join-Path $log 'heldleg_cmp_seeds.txt')
    Write-Output ((Get-Content (Join-Path $log 'heldleg_cmp_seeds.txt') | Select-Object -First 12) -join "`n")
}
else { Write-Output 'no seeds folder under Saved\Checks\n2_heldleg1' }
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_heldleg_near -Near 70 -Tune $on *> (Join-Path $log 'heldleg_near.txt')
Write-Output ((Get-Content (Join-Path $log 'heldleg_near.txt') | Select-Object -First 2) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\getup_seeds\n2_heldleg_near -Name r10on_near *> (Join-Path $log 'heldleg_cmp_near.txt')
Write-Output ((Get-Content (Join-Path $log 'heldleg_cmp_near.txt') | Select-Object -First 12) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_heldleg_command -Near 70 -CommandAtS 3 -Tune $on *> (Join-Path $log 'heldleg_command.txt')
Write-Output ((Get-Content (Join-Path $log 'heldleg_command.txt') | Select-Object -First 2) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\getup_seeds\n2_heldleg_command -Name r10on_command *> (Join-Path $log 'heldleg_cmp_command.txt')
Write-Output ((Get-Content (Join-Path $log 'heldleg_cmp_command.txt') | Select-Object -First 12) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_heldleg_hold1 -HoldHands 1 -Tune $on *> (Join-Path $log 'heldleg_hold1.txt')
Write-Output ((Get-Content (Join-Path $log 'heldleg_hold1.txt') | Select-Object -First 2) -join "`n")
# Held against the stand after step 9's repair of R2's held arm (n2_heldpole_hold1), not r10on_hold1 (16 runs moved by that repair).
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -SaveRuns Saved\Matrix\getup_seeds\n2_heldpole_hold1 -Name n2_hold1_runs *> $null
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\getup_seeds\n2_heldleg_hold1 -Name n2_hold1_runs *> (Join-Path $log 'heldleg_cmp_hold1.txt')
Write-Output ((Get-Content (Join-Path $log 'heldleg_cmp_hold1.txt') | Select-Object -First 3) -join "`n")
'HELDLEG GATE DONE'
