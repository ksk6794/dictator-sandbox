# behaviour.command_cover_react (R10's gate, C39; 28 November): the suite at 0 against n2reach (every hash), the suite with it on (what
# moves), and the stand with a viewer and get_up 3 s after it came with it on, against r10on_command.
#   powershell -ExecutionPolicy Bypass -File docs\recon\night2\cover_react.ps1
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$log = Join-Path $repo 'Saved\Recon\night2'
& powershell -ExecutionPolicy Bypass -File Tools\suite.ps1 -Baseline n2reach *> (Join-Path $log 'coverreact_off.txt')
Write-Output ((Get-Content (Join-Path $log 'coverreact_off.txt') | Where-Object { $_ -match '^SUITE|^BASELINE|changed:' }) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -SaveRuns Saved\Baselines\n2reach\scenarios -Name n2reach_runs *> $null
& powershell -ExecutionPolicy Bypass -File Tools\suite.ps1 -Tune 'behaviour.command_cover_react=1' -Label n2_coverreact *> (Join-Path $log 'coverreact_on.txt')
Write-Output ((Get-Content (Join-Path $log 'coverreact_on.txt') | Where-Object { $_ -match '^SUITE' }) -join ' ')
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Suites\n2_coverreact -Name n2reach_runs *> (Join-Path $log 'coverreact_cmp.txt')
Write-Output ((Get-Content (Join-Path $log 'coverreact_cmp.txt') | Select-Object -First 30) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label n2_coverreact_command -Near 70 -CommandAtS 3 -Tune 'behaviour.command_cover_react=1' *> (Join-Path $log 'coverreact_command.txt')
Write-Output ((Get-Content (Join-Path $log 'coverreact_command.txt') | Select-Object -First 1) -join "`n")
Write-Output ((Get-Content (Join-Path $log 'coverreact_command.txt') | Select-String -Pattern 'command|C39' | Select-Object -First 6 | ForEach-Object { $_.Line }) -join "`n")
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareRuns Saved\Matrix\getup_seeds\n2_coverreact_command -Name r10on_command *> (Join-Path $log 'coverreact_cmp_command.txt')
Write-Output ((Get-Content (Join-Path $log 'coverreact_cmp_command.txt') | Select-Object -First 1) -join "`n")
'COVER REACT DONE'
