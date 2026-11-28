# The struggle switched on (behaviour.struggle=1 by -Tune) on the files of the window's end (protect_held_leg on): the suite held run by
# run against the files' suite (n2heldleg), and the stand held by two hands on three seeds (decision 13: at most 5 of 150 pull free),
# held against the same stand with the struggle off.
#   powershell -ExecutionPolicy Bypass -File docs\recon\night2\struggle_on.ps1
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$log = Join-Path $repo 'Saved\Recon\night2'
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -Save n2heldleg *> (Join-Path $log 'struggle_on_save.txt')
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -SaveRuns Saved\Baselines\n2heldleg\scenarios -Name n2heldleg_runs *> $null
& powershell -ExecutionPolicy Bypass -File Tools\suite.ps1 -Tune 'behaviour.struggle=1' -Label n2_struggle_on *> (Join-Path $log 'struggle_on_suite.txt')
Write-Output ((Get-Content (Join-Path $log 'struggle_on_suite.txt') | Where-Object { $_ -match '^SUITE' }) -join ' ')
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Suites\n2_struggle_on -Name n2heldleg_runs *> (Join-Path $log 'struggle_on_cmp.txt')
Write-Output ((Get-Content (Join-Path $log 'struggle_on_cmp.txt') | Select-Object -First 40) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_hold2_off -HoldHands 2 *> (Join-Path $log 'struggle_hold2_off.txt')
Write-Output ((Get-Content (Join-Path $log 'struggle_hold2_off.txt') | Select-Object -First 2) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -SaveRuns Saved\Matrix\getup_seeds\n2_hold2_off -Name n2_hold2_off *> $null
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_hold2_on -HoldHands 2 -Tune 'behaviour.struggle=1' *> (Join-Path $log 'struggle_hold2_on.txt')
Write-Output ((Get-Content (Join-Path $log 'struggle_hold2_on.txt') | Select-Object -First 2) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\getup_seeds\n2_hold2_on -Name n2_hold2_off *> (Join-Path $log 'struggle_hold2_cmp.txt')
Write-Output ((Get-Content (Join-Path $log 'struggle_hold2_cmp.txt') | Select-Object -First 3) -join "`n")
'STRUGGLE ON DONE'
