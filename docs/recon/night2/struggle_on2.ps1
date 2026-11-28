# The struggle's reach at 45 and 70 cm in the files (28 November): the suite against n2heldleg (only the scenarios that switch the struggle
# on themselves may move), the suite with the struggle on, and the stand held by two hands with it on (decision 13), against n2_hold2_off.
#   powershell -ExecutionPolicy Bypass -File docs\recon\night2\struggle_on2.ps1
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$log = Join-Path $repo 'Saved\Recon\night2'
& powershell -ExecutionPolicy Bypass -File Tools\suite.ps1 -Baseline n2heldleg *> (Join-Path $log 'reach_files_suite.txt')
Write-Output ((Get-Content (Join-Path $log 'reach_files_suite.txt') | Where-Object { $_ -match '^SUITE|^BASELINE|changed:' }) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\suite.ps1 -Tune 'behaviour.struggle=1' -Label n2_struggle_on2 *> (Join-Path $log 'reach_on_suite.txt')
Write-Output ((Get-Content (Join-Path $log 'reach_on_suite.txt') | Where-Object { $_ -match '^SUITE' }) -join ' ')
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Suites\n2_struggle_on2 -Name n2heldleg_runs *> (Join-Path $log 'reach_on_cmp.txt')
Write-Output ((Get-Content (Join-Path $log 'reach_on_cmp.txt') | Select-Object -First 20) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_hold2_on2 -HoldHands 2 -Tune 'behaviour.struggle=1' *> (Join-Path $log 'reach_hold2_on.txt')
Write-Output ((Get-Content (Join-Path $log 'reach_hold2_on.txt') | Select-Object -First 1) -join "`n")
Write-Output ((Get-Content (Join-Path $log 'reach_hold2_on.txt') | Select-String -Pattern '^\s+seed \d' | ForEach-Object { $t = $_.Line; $i = $t.IndexOf('a grip came off'); $t.Substring($i, [Math]::Min(120, $t.Length - $i)) }) -join "`n")
'REACH ON DONE'
