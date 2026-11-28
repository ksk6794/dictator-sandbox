# Stage 2c of docs/recon/window3_plan.md: the hand's bodies fitted to the visible hand (effectors.hand.mesh_fit), measured against
# the files (the ball of 4.5 cm and the plate of 18 x 9 x 3): the suite run by run, the grab's 93 pulls, the hand's lag, the record
# and replay of the three takes and of the walk, each with the fit and without.
#   powershell -ExecutionPolicy Bypass -File docs\recon\window3\step2c_measure.ps1
param([string]$Fit = 'effectors.hand.mesh=1,effectors.hand.mesh_fit=1', [string]$Seen = 'effectors.hand.mesh=1')
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$log = Join-Path $repo 'Saved\Recon\window3'
New-Item -ItemType Directory -Force $log | Out-Null
function Invoke-Logged([string]$Name, [string[]]$ToolArgs) {
    $file = Join-Path $log "$Name.txt"
    & powershell -ExecutionPolicy Bypass -File @ToolArgs *> $file
    $first = Get-Content $file | Where-Object { $_ -match '^[A-Z][A-Z ]+(OK|FAIL|FAILED|ERROR)' } | Select-Object -First 2
    Write-Output ("{0}: {1}" -f $Name, ($first -join ' | '))
}
Invoke-Logged 'fit_checks_on' @('Tools\checks.ps1', '-What', 'suite,grabs', '-Tune', $Fit, '-Label', 'w3_fit_on')
Invoke-Logged 'fit_checks_seen' @('Tools\checks.ps1', '-What', 'grabs', '-Tune', $Seen, '-Label', 'w3_fit_seen')
Invoke-Logged 'fit_swing_seen' @('Tools\swing-lag.ps1', '-Tune', $Seen)
Copy-Item (Join-Path $repo 'Saved\Tools\swing-lag.json') (Join-Path $log 'swing_lag_seen.json') -Force -ErrorAction SilentlyContinue
Invoke-Logged 'fit_swing_on' @('Tools\swing-lag.ps1', '-Tune', $Fit)
Copy-Item (Join-Path $repo 'Saved\Tools\swing-lag.json') (Join-Path $log 'swing_lag_fit.json') -Force -ErrorAction SilentlyContinue
Invoke-Logged 'fit_takes_seen' @('Tools\record-replay.ps1', '-Takes', '-Tune', $Seen)
Invoke-Logged 'fit_takes_on' @('Tools\record-replay.ps1', '-Takes', '-Tune', $Fit)
Invoke-Logged 'fit_walk_on' @('Tools\record-replay.ps1', '-Walk', '-Twice', '-Tune', $Fit)
'STEP2C MEASURE DONE'
