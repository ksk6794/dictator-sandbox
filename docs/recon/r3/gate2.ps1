# R3's gate for A and B again, after the second reading's fixes (the elbow's anatomical hinge for the catch).
$ErrorActionPreference = 'Continue'
Set-Location (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$A = 'fall.catch=1,fall.arm_curve=1,fall.head=1,fall.palms=1'
$B = "$A,fall.blend=1"
$r = 'Saved\Recon\r3'
foreach ($v in @(@{ L = 'r3Ag2'; T = $A }, @{ L = 'r3Bg2'; T = $B })) {
    $L = $v.L; $T = $v.T
    powershell -ExecutionPolicy Bypass -File Tools\checks.ps1 -What "suite,matrix,knockdowns,seeds,natural_quick" -Tune $T -Label $L *> "$r\checks_$L.txt"
    powershell -ExecutionPolicy Bypass -File Tools\knockdowns.ps1 -ByHand -Tune $T -Out "Saved\Matrix\knockdowns_by_hand_$L" *> "$r\kdh_$L.txt"
    powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label "${L}_hold1" -HoldHands 1 -Tune $T *> "$r\seeds_${L}_hold1.txt"
    powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label "${L}_press" -PressN 300 -Tune $T *> "$r\seeds_${L}_press.txt"
    if ($L -eq 'r3Ag2') {
        powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label "${L}_hold2" -HoldHands 2 -Tune $T *> "$r\seeds_${L}_hold2.txt"
    }
}
powershell -ExecutionPolicy Bypass -File Tools\sweep.ps1 -Name r3big9 -Files "$r\big" -Sets "$A|$B" -Metrics "control.pose.target_step_max_deg" *> "$r\sweep_big9.txt"
'GATE2 DONE'
