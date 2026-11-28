# Stage 2c of docs/recon/window3_plan.md, the blows: the knockdowns by two fists and the owner's own punches of 25 November replayed
# (Tools\recording-report.ps1 on the vr_step4* recordings), with the visible hand alone and with its bodies fitted to it.
#   powershell -ExecutionPolicy Bypass -File docs\recon\window3\step2c_blows.ps1
param([string]$Fit = 'effectors.hand.mesh=1,effectors.hand.mesh_fit=1', [string]$Seen = 'effectors.hand.mesh=1')
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$log = Join-Path $repo 'Saved\Recon\window3'
New-Item -ItemType Directory -Force $log | Out-Null
foreach ($run in @(@('seen', $Seen), @('fit', $Fit))) {
    & powershell -ExecutionPolicy Bypass -File Tools\knockdowns.ps1 -ByHand -Tune $run[1] *> (Join-Path $log "blows_byhand_$($run[0]).txt")
    Copy-Item (Join-Path $repo 'Saved\Tools\knockdowns.json') (Join-Path $log "blows_byhand_$($run[0]).json") -Force -ErrorAction SilentlyContinue
    & powershell -ExecutionPolicy Bypass -File Tools\recording-report.ps1 -Name 'vr_step4*' -Tune $run[1] *> (Join-Path $log "blows_report_$($run[0]).txt")
    Write-Output ("{0}: {1} | {2}" -f $run[0], (Get-Content (Join-Path $log "blows_byhand_$($run[0]).txt") | Select-Object -First 1), (Get-Content (Join-Path $log "blows_report_$($run[0]).txt") | Select-Object -First 1))
}
'STEP2C BLOWS DONE'
