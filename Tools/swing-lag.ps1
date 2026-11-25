# How far a hand lags behind its target, by experiment (M4 part 1, step 3): the hand is a body with mass on a
# spring with a force cap, and what it is given is a target and the target's velocity. Two measurements:
#
# 1. Scripted swings. The right hand's target rests, travels -DistanceCm along a straight line with a smooth
#    (minimum-jerk) speed profile that peaks at each of -Speeds, and rests again. Every swing is run once for
#    each way of telling the spring the target's velocity:
#      exact       the target's true velocity
#      difference  the difference of the last two target positions (what a source without velocities has)
#      runtime     as the headset's runtime gave it in the first session: late by -LagMs and smaller by -Gain
#      none        no velocity at all
#    Reported: the largest gap between hand and target during the swing, the gap when the target is fastest,
#    the largest gap after the target has stopped (the hand arriving late or overshooting), and the largest
#    force the spring law asked for.
# 2. A recording of a person's hands (-Recording <file>, optional; -FromS and -ToS bound the part that counts).
#    It is replayed as recorded (the runtime's velocities), with the velocities replaced by the difference of
#    its own positions, and with none. Reported for the frames in which the hand touches nothing: the gap by
#    how fast the target was moving.
#
#   powershell -ExecutionPolicy Bypass -File Tools\swing-lag.ps1
#   ... -Speeds "2,4,6,8" -DistanceCm 60
#   ... -Recording Saved\Recordings\vr_check\vr_check.recording.json -FromS 26 -ToS 61
#   ... -Tune "effectors.hand.max_force_n=600"     other values of the hand
#   ... -Out <dir>                                  default Saved\Matrix\swing_lag
# Prints the tables and writes <Out>\swing-lag.json. It measures and judges nothing. Exit code: 0 measured, 2 a
# run gave no result.
param(
    [string]$Speeds = '2,4,6,8',
    [double]$DistanceCm = 60,
    [double]$LagMs = 38,
    [double]$Gain = 0.78,
    [string]$Recording,
    [double]$FromS = 0,
    [double]$ToS = 0,
    [string]$Tune,
    [string]$Out
)
. "$PSScriptRoot\common.ps1"

if (-not $Out) { $Out = Join-Path $RepoRoot 'Saved\Matrix\swing_lag' }
$Out = [System.IO.Path]::GetFullPath($Out)
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
New-Item -ItemType Directory -Force $Out | Out-Null

$fps = 90
$dt = 1.0 / $fps
$inv = [System.Globalization.CultureInfo]::InvariantCulture
function Format-Number([double]$Value) { return $Value.ToString('R', $inv) }

# The scene: the test room's own scenario, with the hands' targets from a recording. The swings are made
# 1.6 m and more in front of the character, which stands and is not touched.
$scene = Read-JsonFile (Join-Path $RepoRoot 'Scenarios\replay_demo_hands.json')

function Save-Recording {
    # $Frames: an array of frames, each an array of two targets of 11 numbers (x y z, quaternion, velocity, grip).
    param([string]$Path, [array]$Frames)
    $text = New-Object System.Text.StringBuilder
    [void]$text.Append("{`n  `"format`": 1,`n  `"fps`": $fps,`n  `"effectors`": [`"hand_left`", `"hand_right`"],`n")
    [void]$text.Append("  `"columns`": `"x_cm,y_cm,z_cm,qx,qy,qz,qw,vx_cm_s,vy_cm_s,vz_cm_s,grip`",`n  `"frames`": [`n")
    for ($i = 0; $i -lt $Frames.Count; $i++) {
        $hands = @()
        foreach ($hand in $Frames[$i]) { $hands += '[' + (($hand | ForEach-Object { Format-Number ([double]$_) }) -join ',') + ']' }
        $comma = ','
        if ($i -eq $Frames.Count - 1) { $comma = '' }
        [void]$text.Append('[' + ($hands -join ',') + ']' + $comma + "`n")
    }
    [void]$text.Append("  ]`n}`n")
    [System.IO.File]::WriteAllText($Path, $text.ToString(), (New-Object System.Text.UTF8Encoding($false)))
}

function Save-Scenario {
    param([string]$Name, [string]$RecordingPath, [double]$DurationS)
    $spec = [ordered]@{
        name              = $Name
        map               = $scene.map
        duration_s        = $DurationS
        body              = $scene.body
        camera            = $scene.camera
        recording         = $RecordingPath
        recording_start_s = 0.0
        events            = @()
        expect            = @{}
    }
    $file = Join-Path $Out "$Name.json"
    Write-JsonFile $file $spec
    return $file
}

function Read-Hands {
    # hands.csv of a run as rows of doubles by column name.
    param([string]$Dir)
    $file = Join-Path $Dir 'hands.csv'
    if (-not (Test-Path $file)) { return $null }
    $lines = [System.IO.File]::ReadAllLines($file)
    $names = $lines[0].Split(',')
    $rows = New-Object System.Collections.ArrayList
    for ($i = 1; $i -lt $lines.Length; $i++) {
        if (-not $lines[$i]) { continue }
        $cells = $lines[$i].Split(',')
        $row = @{}
        for ($c = 0; $c -lt $names.Length; $c++) { $row[$names[$c]] = [double]::Parse($cells[$c], $inv) }
        [void]$rows.Add($row)
    }
    return $rows
}

$jobs = @()
$result = [ordered]@{ distance_cm = $DistanceCm; lag_ms = $LagMs; gain = $Gain; tune = $Tune; swings = @(); recording = $null }

# ---------------------------------------------------------------------------------------------- scripted swings
$restBeforeS = 1.0
$restAfterS = 0.8
$modes = @('exact', 'difference', 'runtime', 'none')
$startPoint = @(40.0, 160.0 + $DistanceCm, 130.0)
$leftRest = @(-40.0, 220.0, 130.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0)
$swings = @()
foreach ($speedText in $Speeds.Split(',')) {
    $speed = [double]::Parse($speedText.Trim(), $inv)
    # Minimum jerk: s = 10u^3 - 15u^4 + 6u^5, whose speed peaks at 1.875 D / T in the middle.
    $durationS = 1.875 * ($DistanceCm / 100.0) / $speed
    $total = [int][Math]::Ceiling(($restBeforeS + $durationS + $restAfterS) * $fps)
    $place = { param($t) $u = [Math]::Min([Math]::Max(($t - $restBeforeS) / $durationS, 0.0), 1.0); return $DistanceCm * (10 * [Math]::Pow($u, 3) - 15 * [Math]::Pow($u, 4) + 6 * [Math]::Pow($u, 5)) }
    $rate = { param($t) $u = ($t - $restBeforeS) / $durationS; if ($u -le 0.0 -or $u -ge 1.0) { return 0.0 }; return $DistanceCm / $durationS * (30 * $u * $u - 60 * [Math]::Pow($u, 3) + 30 * [Math]::Pow($u, 4)) }
    foreach ($mode in $modes) {
        $frames = New-Object System.Collections.ArrayList
        for ($i = 0; $i -lt $total; $i++) {
            $t = $i * $dt
            $moved = & $place $t
            $velocity = 0.0
            if ($mode -eq 'exact') { $velocity = & $rate $t }
            elseif ($mode -eq 'difference' -and $i -gt 0) { $velocity = ($moved - (& $place ($t - $dt))) / $dt }
            elseif ($mode -eq 'runtime') { $velocity = $Gain * (& $rate ($t - $LagMs / 1000.0)) }
            # The swing goes along -y, toward the character.
            $right = @($startPoint[0], ($startPoint[1] - $moved), $startPoint[2], 0.0, 0.0, 0.0, 1.0, 0.0, (-$velocity), 0.0, 0.0)
            [void]$frames.Add(@($leftRest, $right))
        }
        $name = ('swing_{0}mps_{1}' -f $speedText.Trim().Replace('.', '_'), $mode)
        $recordingFile = Join-Path $Out "$name.recording.json"
        Save-Recording -Path $recordingFile -Frames $frames
        $file = Save-Scenario -Name $name -RecordingPath $recordingFile -DurationS ($total * $dt)
        $job = @{ File = $file; Out = (Join-Path $Out $name); Tune = $Tune }
        $jobs += $job
        $swings += @{ Speed = $speed; Mode = $mode; DurationS = $durationS; Dir = $job.Out; Name = $name }
    }
}

# ------------------------------------------------------------------------------------------- a person's recording
$person = @()
$recorded = $null
if ($Recording) {
    $recordingPath = [System.IO.Path]::GetFullPath($Recording)
    $recorded = Read-JsonFile $recordingPath
    if (-not $recorded -or [int]$recorded.fps -ne $fps) {
        Write-Output "SWING LAG ERROR: $recordingPath is not a recording at $fps frames a second"
        exit 2
    }
    $count = $recorded.frames.Count
    if ($ToS -le 0) { $ToS = $count * $dt }
    foreach ($mode in @('runtime', 'difference', 'none')) {
        $frames = New-Object System.Collections.ArrayList
        for ($i = 0; $i -lt $count; $i++) {
            $pair = @()
            for ($h = 0; $h -lt 2; $h++) {
                $now = $recorded.frames[$i][$h]
                $target = @($now[0], $now[1], $now[2], $now[3], $now[4], $now[5], $now[6], $now[7], $now[8], $now[9], $now[10])
                if ($mode -ne 'runtime') {
                    for ($k = 0; $k -lt 3; $k++) {
                        $target[7 + $k] = 0.0
                        if ($mode -eq 'difference' -and $i -gt 0) { $target[7 + $k] = ([double]$now[$k] - [double]$recorded.frames[$i - 1][$h][$k]) / $dt }
                    }
                }
                $pair += , $target
            }
            [void]$frames.Add($pair)
        }
        $name = "person_$mode"
        $recordingFile = Join-Path $Out "$name.recording.json"
        Save-Recording -Path $recordingFile -Frames $frames
        $file = Save-Scenario -Name $name -RecordingPath $recordingFile -DurationS ($count * $dt)
        $job = @{ File = $file; Out = (Join-Path $Out $name); Tune = $Tune }
        $jobs += $job
        $person += @{ Mode = $mode; Dir = $job.Out }
    }
}

Invoke-ScenarioBatch -Jobs $jobs -Parallel 8

# ------------------------------------------------------------------------------------------------------ results
$failed = $false
Write-Output ("SWING LAG: the right hand behind a target that travels {0:N0} cm, by how the spring is told the target's velocity" -f $DistanceCm)
Write-Output '  peak speed  velocity     gap in the swing  gap at the peak  gap after the stop  within 1 cm after  spring asked for'
foreach ($swing in $swings) {
    $rows = Read-Hands $swing.Dir
    if (-not $rows) { Write-Output "  no hands.csv in $($swing.Dir)"; $failed = $true; continue }
    $from = $restBeforeS
    $to = $restBeforeS + $swing.DurationS
    $peakT = $restBeforeS + $swing.DurationS / 2
    $inSwing = 0.0; $atPeak = 0.0; $after = 0.0; $demand = 0.0; $settled = $null; $peakOff = 1.0e9
    foreach ($row in $rows) {
        $t = $row['t']; $gap = $row['hand_right_gap_cm']
        if ($t -ge $from -and $t -le $to + 0.5 * $dt) {
            $inSwing = [Math]::Max($inSwing, $gap)
            if ([Math]::Abs($t - $peakT) -lt $peakOff) { $peakOff = [Math]::Abs($t - $peakT); $atPeak = $gap }
        }
        if ($t -gt $to + 0.5 * $dt) {
            $after = [Math]::Max($after, $gap)
            if ($gap -gt 1.0) { $settled = $null } elseif ($null -eq $settled) { $settled = $t - $to }
        }
        if ($t -ge $from) { $demand = [Math]::Max($demand, $row['hand_right_demand_n']) }
    }
    $settledText = 'never'
    if ($null -ne $settled) { $settledText = ('{0:N2} s' -f $settled) }
    Write-Output ("  {0,5:N1} m/s  {1,-10}  {2,13:N1} cm  {3,12:N1} cm  {4,15:N1} cm  {5,17}  {6,14:N0} N" -f $swing.Speed, $swing.Mode, $inSwing, $atPeak, $after, $settledText, $demand)
    $result.swings += [ordered]@{ peak_speed_mps = $swing.Speed; velocity = $swing.Mode; swing_s = $swing.DurationS; max_gap_cm = $inSwing; gap_at_peak_cm = $atPeak; max_gap_after_cm = $after; settled_s = $settled; max_demand_n = $demand }
}

if ($recorded) {
    $edges = @(0.0, 0.5, 1.0, 2.0, 3.0, 100.0)
    $personResult = [ordered]@{ file = $Recording; from_s = $FromS; to_s = $ToS; runs = @() }
    Write-Output ("  a person's hands ({0}, {1:N0} to {2:N0} s), frames in which the hand touches nothing: mean gap by the target's speed" -f (Split-Path -Leaf $Recording), $FromS, $ToS)
    Write-Output '  velocity      frames   under 0.5 m/s   0.5 to 1   1 to 2   2 to 3   over 3 m/s   95th percentile   largest'
    foreach ($run in $person) {
        $rows = Read-Hands $run.Dir
        if (-not $rows) { Write-Output "  no hands.csv in $($run.Dir)"; $failed = $true; continue }
        $sum = @(0.0, 0.0, 0.0, 0.0, 0.0); $num = @(0, 0, 0, 0, 0)
        $gaps = New-Object System.Collections.ArrayList
        $h = 0
        foreach ($hand in @('hand_left', 'hand_right')) {
            $quiet = 0
            foreach ($row in $rows) {
                # Frames after a contact are left out for a sixth of a second: the hand is still coming back.
                if ($row["${hand}_contact_n"] -gt 0.0) { $quiet = 15 } elseif ($quiet -gt 0) { $quiet-- }
                $t = $row['t']
                $frame = [int][Math]::Round($t * $fps)
                if ($t -lt $FromS -or $t -gt $ToS -or $quiet -gt 0 -or $frame -lt 1 -or $frame -ge $recorded.frames.Count) { continue }
                $a = $recorded.frames[$frame][$h]; $b = $recorded.frames[$frame - 1][$h]
                $speed = [Math]::Sqrt([Math]::Pow([double]$a[0] - [double]$b[0], 2) + [Math]::Pow([double]$a[1] - [double]$b[1], 2) + [Math]::Pow([double]$a[2] - [double]$b[2], 2)) / $dt / 100.0
                $gap = $row["${hand}_gap_cm"]
                for ($e = 0; $e -lt 5; $e++) { if ($speed -ge $edges[$e] -and $speed -lt $edges[$e + 1]) { $sum[$e] += $gap; $num[$e]++ } }
                [void]$gaps.Add($gap)
            }
            $h++
        }
        $sorted = @($gaps | Sort-Object)
        $means = @()
        for ($e = 0; $e -lt 5; $e++) { if ($num[$e] -gt 0) { $means += $sum[$e] / $num[$e] } else { $means += 0.0 } }
        $p95 = 0.0; $largest = 0.0
        if ($sorted.Count -gt 0) { $p95 = $sorted[[int][Math]::Floor(($sorted.Count - 1) * 0.95)]; $largest = $sorted[-1] }
        Write-Output ("  {0,-10}  {1,7}  {2,10:N2} cm  {3,6:N2} cm  {4,4:N2} cm  {5,4:N2} cm  {6,8:N2} cm  {7,13:N2} cm  {8,6:N1} cm" -f $run.Mode, $sorted.Count, $means[0], $means[1], $means[2], $means[3], $means[4], $p95, $largest)
        Write-Output ("              frames by speed: {0}" -f ($num -join ', '))
        $personResult.runs += [ordered]@{ velocity = $run.Mode; frames = $sorted.Count; frames_by_speed = $num; mean_gap_cm_by_speed = $means; p95_gap_cm = $p95; max_gap_cm = $largest }
    }
    $result.recording = $personResult
}

Write-JsonFile (Join-Path $Out 'swing-lag.json') $result
Write-Output "  result: $(Join-Path $Out 'swing-lag.json')"
if ($failed) { exit 2 }
exit 0
