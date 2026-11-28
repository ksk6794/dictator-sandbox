# After a change that is to keep every hash (the night's step 4, the arbiter of limbs, recorded with one rule moved into it): the files'
# body through one check set (suite, matrix, knockdowns, stand on three seeds), the suite with R3's A on (where the catch's asks are
# recorded), the stands with a viewer and with the command, and the seeing replay; then each held against what was kept before it.
#   powershell -ExecutionPolicy Bypass -File docs\recon\night2\hashes_check.ps1 -Tag arb
param([string]$Tag = 'arb')
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$log = Join-Path $repo 'Saved\Recon\night2'
$A = 'fall.catch=1,fall.arm_curve=1,fall.head=1,fall.palms=1'
& powershell -ExecutionPolicy Bypass -File Tools\checks.ps1 -What 'suite,matrix,knockdowns,seeds' *> (Join-Path $log "checks_$Tag.txt")
Write-Output ((Get-Content (Join-Path $log "checks_$Tag.txt") | Where-Object { $_ -match '^CHECKS|^\s+(suite|matrix|knockdowns|seeds)\s' }) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -Compare r10on *> (Join-Path $log "cmp_suite_$Tag.txt")
Write-Output ((Get-Content (Join-Path $log "cmp_suite_$Tag.txt") | Select-Object -First 1))
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\push\runs -Name r10on_matrix *> (Join-Path $log "cmp_matrix_$Tag.txt")
Write-Output ((Get-Content (Join-Path $log "cmp_matrix_$Tag.txt") | Select-Object -First 1))
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\getup_seeds\seeds -Name r10on_seeds *> (Join-Path $log "cmp_seeds_$Tag.txt")
Write-Output ((Get-Content (Join-Path $log "cmp_seeds_$Tag.txt") | Select-Object -First 1))
& powershell -ExecutionPolicy Bypass -File Tools\checks.ps1 -What 'suite' -Tune $A -Label "n2_${Tag}_A" *> (Join-Path $log "checks_${Tag}_A.txt")
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns "Saved\Suites\checks_n2_${Tag}_A" -Name n2_r3A_suite *> (Join-Path $log "cmp_suiteA_$Tag.txt")
Write-Output ((Get-Content (Join-Path $log "cmp_suiteA_$Tag.txt") | Select-Object -First 1))
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label "n2_${Tag}_near" -Near 70 *> (Join-Path $log "seeds_near_$Tag.txt")
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns "Saved\Matrix\getup_seeds\n2_${Tag}_near" -Name r10on_near *> (Join-Path $log "cmp_near_$Tag.txt")
Write-Output ((Get-Content (Join-Path $log "cmp_near_$Tag.txt") | Select-Object -First 1))
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label "n2_${Tag}_command" -Near 70 -CommandAtS 3 *> (Join-Path $log "seeds_command_$Tag.txt")
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns "Saved\Matrix\getup_seeds\n2_${Tag}_command" -Name r10on_command *> (Join-Path $log "cmp_command_$Tag.txt")
Write-Output ((Get-Content (Join-Path $log "cmp_command_$Tag.txt") | Select-Object -First 1))
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label "n2_${Tag}_hold1" -HoldHands 1 *> (Join-Path $log "seeds_hold1_$Tag.txt")
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns "Saved\Matrix\getup_seeds\n2_${Tag}_hold1" -Name r10on_hold1 *> (Join-Path $log "cmp_hold1_$Tag.txt")
Write-Output ((Get-Content (Join-Path $log "cmp_hold1_$Tag.txt") | Select-Object -First 1))
& powershell -ExecutionPolicy Bypass -File Tools\record-replay.ps1 -Perception *> (Join-Path $log "rr_seeing_$Tag.txt")
Write-Output ((Get-Content (Join-Path $log "rr_seeing_$Tag.txt") | Select-Object -First 1))
'HASHES CHECK DONE'
