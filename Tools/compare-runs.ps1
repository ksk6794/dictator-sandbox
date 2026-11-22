# Compares two scenario output directories (each holding metrics.json and trace.json).
#   powershell -ExecutionPolicy Bypass -File Tools\compare-runs.ps1 -A <dir> -B <dir>
#   ... -PositionToleranceCm 0.01 -RotationToleranceDeg 0.01     accept small deviations
# Reports whether the per-frame hashes match and the largest deviation of any body or bone at any
# checkpoint. Exit code: 0 identical or within tolerance, 1 outside tolerance, 2 not comparable.
param(
    [Parameter(Mandatory = $true)][string]$A,
    [Parameter(Mandatory = $true)][string]$B,
    [double]$PositionToleranceCm = 0,
    [double]$RotationToleranceDeg = 0,
    [string]$Result,
    [switch]$Quiet
)
. "$PSScriptRoot\common.ps1"

function Get-RotationDeltaDeg {
    param($Q1, $Q2)
    $dot = [math]::Abs($Q1[0] * $Q2[0] + $Q1[1] * $Q2[1] + $Q1[2] * $Q2[2] + $Q1[3] * $Q2[3])
    if ($dot -gt 1) { $dot = 1 }
    return (2 * [math]::Acos($dot) * 180 / [math]::PI)
}

function Get-Distance {
    param($P1, $P2)
    $dx = $P1[0] - $P2[0]; $dy = $P1[1] - $P2[1]; $dz = $P1[2] - $P2[2]
    return [math]::Sqrt($dx * $dx + $dy * $dy + $dz * $dz)
}

$metricsA = Read-JsonFile (Join-Path $A 'metrics.json')
$metricsB = Read-JsonFile (Join-Path $B 'metrics.json')
$traceA = Read-JsonFile (Join-Path $A 'trace.json')
$traceB = Read-JsonFile (Join-Path $B 'trace.json')
if (-not $metricsA -or -not $metricsB -or -not $traceA -or -not $traceB) {
    Write-Output "COMPARE ERROR: metrics.json or trace.json missing in '$A' or '$B'"
    exit 2
}
$checkpointsA = @($traceA.checkpoints)
$checkpointsB = @($traceB.checkpoints)
if ($checkpointsA.Count -ne $checkpointsB.Count -or $checkpointsA.Count -eq 0) {
    Write-Output "COMPARE ERROR: checkpoint counts differ ($($checkpointsA.Count) vs $($checkpointsB.Count))"
    exit 2
}

$maxBodyPos = 0.0; $maxBodyRot = 0.0; $maxBonePos = 0.0; $maxBoneRot = 0.0
$worstBody = ''; $firstDivergenceT = $null
for ($i = 0; $i -lt $checkpointsA.Count; $i++) {
    $ca = $checkpointsA[$i]; $cb = $checkpointsB[$i]
    $bodiesA = @($ca.bodies); $bodiesB = @($cb.bodies)
    $bonesA = @($ca.bones); $bonesB = @($cb.bones)
    if ($bodiesA.Count -ne $bodiesB.Count -or $bonesA.Count -ne $bonesB.Count) {
        Write-Output "COMPARE ERROR: body or bone counts differ at checkpoint $i"
        exit 2
    }
    $diverged = $false
    for ($j = 0; $j -lt $bodiesA.Count; $j++) {
        $pos = Get-Distance $bodiesA[$j].p $bodiesB[$j].p
        $rot = Get-RotationDeltaDeg $bodiesA[$j].q $bodiesB[$j].q
        if ($pos -gt $maxBodyPos) { $maxBodyPos = $pos; $worstBody = $bodiesA[$j].bone }
        if ($rot -gt $maxBodyRot) { $maxBodyRot = $rot }
        if ($pos -gt 0 -or $rot -gt 0) { $diverged = $true }
    }
    for ($j = 0; $j -lt $bonesA.Count; $j++) {
        $ba = $bonesA[$j]; $bb = $bonesB[$j]
        $pos = Get-Distance $ba $bb
        $rot = Get-RotationDeltaDeg @($ba[3], $ba[4], $ba[5], $ba[6]) @($bb[3], $bb[4], $bb[5], $bb[6])
        if ($pos -gt $maxBonePos) { $maxBonePos = $pos }
        if ($rot -gt $maxBoneRot) { $maxBoneRot = $rot }
        if ($pos -gt 0 -or $rot -gt 0) { $diverged = $true }
    }
    if ($diverged -and $null -eq $firstDivergenceT) { $firstDivergenceT = $ca.t }
}

$bodyHashEqual = ($metricsA.hashes.bodies -eq $metricsB.hashes.bodies)
$boneHashEqual = ($metricsA.hashes.bones -eq $metricsB.hashes.bones)
$identical = $bodyHashEqual -and $boneHashEqual
$maxPos = [math]::Max($maxBodyPos, $maxBonePos)
$maxRot = [math]::Max($maxBodyRot, $maxBoneRot)
$withinTolerance = ($maxPos -le $PositionToleranceCm) -and ($maxRot -le $RotationToleranceDeg)
$pass = $identical -or $withinTolerance

$summary = [ordered]@{
    pass                   = $pass
    identical              = $identical
    body_hash_equal        = $bodyHashEqual
    bone_hash_equal        = $boneHashEqual
    a                      = [ordered]@{ dir = $A; rendered = $metricsA.rendered; bodies = $metricsA.hashes.bodies; bones = $metricsA.hashes.bones }
    b                      = [ordered]@{ dir = $B; rendered = $metricsB.rendered; bodies = $metricsB.hashes.bodies; bones = $metricsB.hashes.bones }
    checkpoints            = $checkpointsA.Count
    max_body_pos_dev_cm    = $maxBodyPos
    max_body_rot_dev_deg   = $maxBodyRot
    max_bone_pos_dev_cm    = $maxBonePos
    max_bone_rot_dev_deg   = $maxBoneRot
    worst_body             = $worstBody
    first_divergence_t     = $firstDivergenceT
    position_tolerance_cm  = $PositionToleranceCm
    rotation_tolerance_deg = $RotationToleranceDeg
}
if ($Result) { Write-JsonFile $Result $summary }

if (-not $Quiet) {
    if ($identical) {
        Write-Output "COMPARE IDENTICAL: per-frame hashes match (bodies $($metricsA.hashes.bodies), bones $($metricsA.hashes.bones))"
    } else {
        $verdict = 'DIFFERENT'
        if ($pass) { $verdict = 'WITHIN TOLERANCE' }
        Write-Output ("COMPARE {0}: body hashes equal={1}, bone hashes equal={2}; max deviation bodies {3:G4} cm / {4:G4} deg, bones {5:G4} cm / {6:G4} deg; first divergence at t={7}" -f `
            $verdict, $bodyHashEqual, $boneHashEqual, $maxBodyPos, $maxBodyRot, $maxBonePos, $maxBoneRot, $firstDivergenceT)
    }
}
if ($pass) { exit 0 }
exit 1
