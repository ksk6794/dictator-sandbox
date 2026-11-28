# After the second reading's fixes (28 November): the suite in the files against the files' last suite (n2cover, saved first), the suite
# with protect_held_leg and command_cover_react at 0 against n2review (the files before either), and the stands with a viewer, with
# get_up and plain on three seeds against the last runs with both on.
#   powershell -ExecutionPolicy Bypass -File docs\recon\night2\review2_check.ps1
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$log = Join-Path $repo 'Saved\Recon\night2'
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -Save n2cover *> (Join-Path $log 'r2c_save.txt')
& powershell -ExecutionPolicy Bypass -File Tools\suite.ps1 -Baseline n2cover *> (Join-Path $log 'r2c_files.txt')
Write-Output ((Get-Content (Join-Path $log 'r2c_files.txt') | Where-Object { $_ -match '^SUITE|^BASELINE|changed:' }) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -SaveRuns Saved\Baselines\n2review\scenarios -Name n2review_runs *> $null
& powershell -ExecutionPolicy Bypass -File Tools\suite.ps1 -Tune 'behaviour.protect_held_leg=0,behaviour.command_cover_react=0' -Label n2_both0 *> (Join-Path $log 'r2c_both0.txt')
Write-Output ((Get-Content (Join-Path $log 'r2c_both0.txt') | Where-Object { $_ -match '^SUITE' }) -join ' ')
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Suites\n2_both0 -Name n2review_runs *> (Join-Path $log 'r2c_both0_cmp.txt')
Write-Output ((Get-Content (Join-Path $log 'r2c_both0_cmp.txt') | Select-Object -First 12) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_final_command -Near 70 -CommandAtS 3 *> (Join-Path $log 'r2c_command.txt')
Write-Output ((Get-Content (Join-Path $log 'r2c_command.txt') | Select-Object -First 1) -join "`n")
Write-Output ((Get-Content (Join-Path $log 'r2c_command.txt') | Select-String -Pattern '^\s+seed \d' | ForEach-Object { $t = $_.Line; $i = $t.IndexOf('get_up told'); $t.Substring($i, [Math]::Min(230, $t.Length - $i)) }) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -SaveRuns Saved\Matrix\getup_seeds\n2_coverreact_command -Name n2_coverreact_command *> $null
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\getup_seeds\n2_final_command -Name n2_coverreact_command *> (Join-Path $log 'r2c_command_cmp.txt')
Write-Output ((Get-Content (Join-Path $log 'r2c_command_cmp.txt') | Select-Object -First 8) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_final_near -Near 70 *> (Join-Path $log 'r2c_near.txt')
Write-Output ((Get-Content (Join-Path $log 'r2c_near.txt') | Select-Object -First 1) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\getup_seeds\n2_final_near -Name r10on_near *> (Join-Path $log 'r2c_near_cmp.txt')
Write-Output ((Get-Content (Join-Path $log 'r2c_near_cmp.txt') | Select-Object -First 4) -join "`n")
'REVIEW2 CHECK DONE'
