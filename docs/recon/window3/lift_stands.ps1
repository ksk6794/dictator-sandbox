# Decision 46's second half: R2's hold by two hands (decision 13: at most 5 of 150 pull free) and the drags (decision 18) at the caps
# of docs/recon/window3/lift_two_hands.ps1. Output to files first, then read.
#   powershell -ExecutionPolicy Bypass -File docs\recon\window3\lift_stands.ps1
param([string]$Caps = '300,400,450')
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$log = Join-Path $repo 'Saved\Recon\window3'
$capList = @($Caps -split ',')
foreach ($cap in $capList) {
    $file = Join-Path $log "lift_hold2_$cap.txt"
    & powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label "w3_hold2_$cap" -HoldHands 2 -Tune "effectors.hand.max_force_n=$cap" *> $file
    Write-Output ("hold by two at {0} N: {1}" -f $cap, (Get-Content $file | Select-Object -First 1))
}
$sets = ($capList | ForEach-Object { "effectors.hand.max_force_n=$_" }) -join '|'
$drags = (Get-ChildItem (Join-Path $repo 'Scenarios') -Filter 'getup_drag_*.json' | ForEach-Object { $_.BaseName }) -join ','
$file = Join-Path $log 'lift_drags.txt'
& powershell -ExecutionPolicy Bypass -File Tools\sweep.ps1 -Name w3_lift_drags -Scenarios $drags -Sets $sets -Metrics 'control.end_state,metrics.max_pelvis_drift_cm' *> $file
Get-Content $file | Where-Object { $_ -match '^SWEEP|^\s+\d+\s' }
'LIFT STANDS DONE'
