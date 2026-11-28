# The window's last measurement (28 November), on the code and files as they are committed: the suite against n2r2 (the files before the
# third reading's fixes), both of the gate's new switches at 0 against n2review, every tool of R10's gate through one check set, the stands
# with a viewer, with get_up, held by one hand and by two, each held run by run against the last runs kept; then the baseline n2end and
# the runs' hashes n2end_*.
#   powershell -ExecutionPolicy Bypass -File docs\recon\night2\final_gate.ps1
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$log = Join-Path $repo 'Saved\Recon\night2'
function Show($file, $lines) { Write-Output ((Get-Content (Join-Path $log $file) | Select-Object -First $lines) -join "`n") }
& powershell -ExecutionPolicy Bypass -File Tools\suite.ps1 -Baseline n2r2 *> (Join-Path $log 'fin_suite.txt')
Write-Output ((Get-Content (Join-Path $log 'fin_suite.txt') | Where-Object { $_ -match '^SUITE|^BASELINE|changed:' }) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -Save n2end *> (Join-Path $log 'fin_save.txt')
Show 'fin_save.txt' 1
& powershell -ExecutionPolicy Bypass -File Tools\suite.ps1 -Tune 'behaviour.protect_held_leg=0,behaviour.command_cover_react=0' -Label n2_fin_both0 *> (Join-Path $log 'fin_both0.txt')
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Suites\n2_fin_both0 -Name n2review_runs *> (Join-Path $log 'fin_both0_cmp.txt')
Show 'fin_both0_cmp.txt' 8
& powershell -ExecutionPolicy Bypass -File Tools\checks.ps1 -What 'matrix,knockdowns,seeds,natural' -Label n2_fin *> (Join-Path $log 'fin_checks.txt')
Write-Output ((Get-Content (Join-Path $log 'fin_checks.txt') | Where-Object { $_ -match '^CHECKS|^\s+(matrix|knockdowns|seeds|natural)\s' }) -join "`n")
# A labelled check set keeps the matrix's runs under Saved\Checks\<label>\matrix\runs.
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Checks\n2_fin\matrix\runs -Name r10on_matrix *> (Join-Path $log 'fin_cmp_matrix.txt')
Show 'fin_cmp_matrix.txt' 1
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\getup_seeds\checks_n2_fin -Name r10on_seeds *> (Join-Path $log 'fin_cmp_seeds.txt')
Show 'fin_cmp_seeds.txt' 1
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_fin_near -Near 70 *> (Join-Path $log 'fin_near.txt')
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\getup_seeds\n2_fin_near -Name r10on_near *> (Join-Path $log 'fin_cmp_near.txt')
Show 'fin_cmp_near.txt' 1
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_fin_command -Near 70 -CommandAtS 3 *> (Join-Path $log 'fin_command.txt')
Show 'fin_command.txt' 1
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\getup_seeds\n2_fin_command -Name n2_coverreact_command *> (Join-Path $log 'fin_cmp_command.txt')
Show 'fin_cmp_command.txt' 1
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_fin_hold1 -HoldHands 1 *> (Join-Path $log 'fin_hold1.txt')
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\getup_seeds\n2_fin_hold1 -Name n2_hold1_runs *> (Join-Path $log 'fin_cmp_hold1.txt')
Show 'fin_cmp_hold1.txt' 1
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_fin_hold2 -HoldHands 2 *> (Join-Path $log 'fin_hold2.txt')
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\getup_seeds\n2_fin_hold2 -Name n2_hold2_off *> (Join-Path $log 'fin_cmp_hold2.txt')
Show 'fin_cmp_hold2.txt' 1
foreach ($p in @(@('seeds', 'Saved\Matrix\getup_seeds\checks_n2_fin'), @('near', 'Saved\Matrix\getup_seeds\n2_fin_near'), @('command', 'Saved\Matrix\getup_seeds\n2_fin_command'), @('hold1', 'Saved\Matrix\getup_seeds\n2_fin_hold1'), @('hold2', 'Saved\Matrix\getup_seeds\n2_fin_hold2'))) {
    & powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -SaveRuns $p[1] -Name ('n2end_' + $p[0]) *> $null
}
'FINAL GATE DONE'
