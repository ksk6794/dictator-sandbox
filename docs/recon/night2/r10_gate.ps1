# R10's gate, its automatic part (docs/recon/r10_plan.md, step 10), on the tree at the end of the window of 28 November:
# - every R10 switch at 0 (by -Tune): the suite held run by run against r10on_before (the tree before R10 was switched on);
# - the files: the suite, the matrix, the knockdowns, the stand on three seeds and the scorecard (C25) through one check set, each held
#   against what was kept (r10on, r10on_matrix, r10on_seeds);
# - C33 (a viewer at 70 cm) and C39 (get_up 3 s after it came) on three seeds, against r10on_near and r10on_command;
# - C35: the drags by the wrists and the ankles with the struggle on, on six seeds (the chance and the vigours drawn).
#   powershell -ExecutionPolicy Bypass -File docs\recon\night2\r10_gate.ps1
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$log = Join-Path $repo 'Saved\Recon\night2'
$off = 'behaviour.fear=0,behaviour.stay_down=0,behaviour.protect=0,behaviour.commands=0,behaviour.give_hand=0,behaviour.struggle=0'
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -SaveRuns Saved\Baselines\r10on_before\scenarios -Name r10on_before_runs *> (Join-Path $log 'gate_save_before.txt')
& powershell -ExecutionPolicy Bypass -File Tools\suite.ps1 -Tune $off -Label n2_gate_off *> (Join-Path $log 'gate_suite_off.txt')
Write-Output ((Get-Content (Join-Path $log 'gate_suite_off.txt') | Where-Object { $_ -match '^SUITE' }) -join ' ')
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Suites\n2_gate_off -Name r10on_before_runs *> (Join-Path $log 'gate_cmp_off.txt')
Write-Output ((Get-Content (Join-Path $log 'gate_cmp_off.txt') | Select-Object -First 1))
& powershell -ExecutionPolicy Bypass -File Tools\checks.ps1 -What 'suite,matrix,knockdowns,seeds,natural' *> (Join-Path $log 'gate_checks.txt')
Write-Output ((Get-Content (Join-Path $log 'gate_checks.txt') | Where-Object { $_ -match '^CHECKS|^\s+(suite|matrix|knockdowns|seeds|natural)\s' }) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -Compare r10on *> (Join-Path $log 'gate_cmp_suite.txt')
Write-Output ((Get-Content (Join-Path $log 'gate_cmp_suite.txt') | Select-Object -First 1))
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\push\runs -Name r10on_matrix *> (Join-Path $log 'gate_cmp_matrix.txt')
Write-Output ((Get-Content (Join-Path $log 'gate_cmp_matrix.txt') | Select-Object -First 1))
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\getup_seeds\seeds -Name r10on_seeds *> (Join-Path $log 'gate_cmp_seeds.txt')
Write-Output ((Get-Content (Join-Path $log 'gate_cmp_seeds.txt') | Select-Object -First 1))
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_gate_near -Near 70 *> (Join-Path $log 'gate_near.txt')
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\getup_seeds\n2_gate_near -Name r10on_near *> (Join-Path $log 'gate_cmp_near.txt')
Write-Output ((Get-Content (Join-Path $log 'gate_cmp_near.txt') | Select-Object -First 1))
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_gate_command -Near 70 -CommandAtS 3 *> (Join-Path $log 'gate_command.txt')
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\getup_seeds\n2_gate_command -Name r10on_command *> (Join-Path $log 'gate_cmp_command.txt')
Write-Output ((Get-Content (Join-Path $log 'gate_cmp_command.txt') | Select-Object -First 1))
& powershell -ExecutionPolicy Bypass -File docs\recon\r10\make_struggle.ps1 -Seeds '1,2,3,4,5,6' -Out 'Saved\Recon\night2\struggle_seeds' *> $null
& powershell -ExecutionPolicy Bypass -File Tools\sweep.ps1 -Name n2_gate_struggle -Files (Join-Path $log 'struggle_seeds') -Sets 'behaviour.struggle_chance=0.5' -Metrics 'effectors.list.hand_left.grab_slips,effectors.list.hand_right.grab_slips,control.pose.command_jump_max_nm,control.end_state' *> (Join-Path $log 'gate_struggle.txt')
Write-Output ((Get-Content (Join-Path $log 'gate_struggle.txt') | Select-Object -First 1))
'R10 GATE DONE'
