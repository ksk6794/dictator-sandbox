# Takes hold of the standing NPC by one body after another and pulls, in several directions and at several
# speeds, and judges every pull by the criteria of M4's grab (docs/PRD.md): the grab's joint and the body's
# own joints are not pulled apart, the hand pulls with no more than its spring can give, and the body gains no
# energy that nobody put in.
#   powershell -ExecutionPolicy Bypass -File Tools\grab-pulls.ps1
#   ... -Bodies "lowerarm_l,hand_l"     bones of the bodies taken hold of
#   ... -Speeds "1,2,4"                 how fast the hand's target leaves, m/s
#   ... -Directions "left,front,up"     where it goes: left (+x), right, front (+y), back, up, down
#   ... -DistanceCm 60 -HoldS 1.5       how far, and how long the hand then stays there before it lets go
#   ... -Tune "name=value,..."          tuning overrides for every run
#   ... -CVars "name=value,..."         console variables for every run
#   ... -Out <dir>                      output directory (default Saved\Matrix\grab_pulls)
#
# Where a body's surface is, is measured, not assumed: a standing run gives each body's place, and a run with
# a hand of 20 N that is sent to the body's middle shows where that hand comes to rest against it. The pulls
# then press the open hand -PressCm into that place, close the grip and leave. An arm on the character's left
# (+x) is come to from the left, one on its right from the right, the trunk, the head and the legs from the front
# (beside a thigh hangs a hand, and the hand would be what is taken); a pull straight back into the body is left
# out, being a push. A pull that took hold of another body than the one it was sent to fails.
#
# Limits (each a parameter): the two points a grab holds together come no further apart than -MaxGrabStretchCm,
# no joint of the body stretches more than -MaxJointStretchCm, the grab's joint carries no more than
# -MaxMeanGrabForceN over the frames it holds (the hand's spring has a cap, and that is what a hand pulls with;
# the limit is for the spring's 300 N and does not follow a -Tune of effectors.hand.max_force_n) and no more than
# -MaxGrabForceN at the end of any one frame (a jolt: the spring's force and the hand's own mass thrown about;
# the engine keeps the force of a frame's last substep only), the body's energy rises by no more than
# -MaxEnergyGainJ. What the body did (stood, stepped, fell) and what the grip did (held, slipped, torn) are
# printed and not judged.
#
# Prints one row per pull, writes <Out>\grab_pulls.json. Exit code: 0 every pull passes, 1 one does not,
# 2 a run produced no result.
param(
    [string]$Bodies = 'lowerarm_l,hand_l,upperarm_l,head,spine_04,thigh_l,calf_l',
    [string]$Speeds = '1,2,4',
    [string]$Directions = 'left,front,back,up,down',
    [double]$DistanceCm = 60.0,
    [double]$HoldS = 1.5,
    [double]$PressCm = 1.5,
    [int]$Parallel = 8,
    [string]$Tune,
    [string]$CVars,
    [string]$Out,
    [double]$MaxGrabStretchCm = 3.0,
    [double]$MaxJointStretchCm = 3.0,
    [double]$MaxGrabForceN = 600.0,
    [double]$MaxMeanGrabForceN = 315.0,
    [double]$MaxEnergyGainJ = 40.0
)
. "$PSScriptRoot\common.ps1"

if (-not $Out) { $Out = Join-Path $RepoRoot 'Saved\Matrix\grab_pulls' }
$Out = [System.IO.Path]::GetFullPath($Out)
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
$scenarioDir = Join-Path $Out 'scenarios'
New-Item -ItemType Directory -Force $scenarioDir | Out-Null

$culture = [System.Globalization.CultureInfo]::InvariantCulture
$speedList = @($Speeds.Split(',') | ForEach-Object { [double]::Parse($_.Trim(), $culture) } | Sort-Object)
$bodyList = @($Bodies.Split(',') | ForEach-Object { $_.Trim() } | Select-Object -Unique)
$ways = [ordered]@{ left = @(1.0, 0.0, 0.0); right = @(-1.0, 0.0, 0.0); front = @(0.0, 1.0, 0.0); back = @(0.0, -1.0, 0.0); up = @(0.0, 0.0, 1.0); down = @(0.0, 0.0, -1.0) }
$wayList = @($Directions.Split(',') | ForEach-Object { $_.Trim() } | Select-Object -Unique)
foreach ($way in $wayList) {
    if (-not $ways.Contains($way)) { Write-Output "GRAB PULLS ERROR: '$way' is no direction (left, right, front, back, up, down)"; exit 2 }
}
if ($HoldS -lt 0.1) { Write-Output 'GRAB PULLS ERROR: -HoldS has to be 0.1 s or more (a path needs keys at increasing times)'; exit 2 }
if (@($speedList | Where-Object { $_ -lt 0.1 }).Count -gt 0) { Write-Output 'GRAB PULLS ERROR: a speed has to be 0.1 m/s or more'; exit 2 }
# A run is named by its speed in tenths of a metre a second: two speeds that round to the same tenth would be one run.
$tenths = @($speedList | ForEach-Object { [int][Math]::Round($_ * 10.0) })
if (@($tenths | Select-Object -Unique).Count -ne $tenths.Count) { Write-Output 'GRAB PULLS ERROR: the speeds have to differ by 0.1 m/s or more'; exit 2 }
if ($DistanceCm -lt 1.0) { Write-Output 'GRAB PULLS ERROR: -DistanceCm has to be 1 cm or more'; exit 2 }
$base = Read-JsonFile (Join-Path $RepoRoot 'Scenarios\stand_npc.json')
$startTime = 3.0
$started = Get-Date

function Add-Vector($a, $b, $scale) { return @(($a[0] + $b[0] * $scale), ($a[1] + $b[1] * $scale), ($a[2] + $b[2] * $scale)) }
function Format-Point($p) { return @([Math]::Round($p[0], 2), [Math]::Round($p[1], 2), [Math]::Round($p[2], 2)) }
function Join-Tune($first, $second) {
    if ($first -and $second) { return "$first,$second" }
    if ($first) { return $first }
    return $second
}
function New-HandEvent($keys) {
    return [ordered]@{ t = $startTime; type = 'effector'; effector = 'hand_right'; path = $keys }
}
function New-Key($time, $point, $grip) {
    return [ordered]@{ t = [Math]::Round($time, 4); location_cm = (Format-Point $point); grip = $grip }
}

# Where every body is while the character stands.
$standDir = Join-Path $Out 'runs\stand'
$standFile = Join-Path $scenarioDir 'grab_stand.json'
Write-JsonFile $standFile ([ordered]@{ name = 'grab_stand'; duration_s = 4.0; body = $base.body; events = @(); expect = [ordered]@{ fell = $false } })
$standJob = @{ File = $standFile; Out = $standDir }
if ($Tune) { $standJob.Tune = $Tune }
if ($CVars) { $standJob.CVars = $CVars }
Invoke-ScenarioBatch -Jobs @($standJob) -Parallel 1
$stand = Read-JsonFile (Join-Path $standDir 'metrics.json')
if (-not $stand -or -not $stand.ok) { Write-Output 'GRAB PULLS ERROR: the standing run produced no result'; exit 2 }
$spawn = @(0.0, 0.0, 0.0)
if ($base.body.location_cm) { $spawn = @([double]$base.body.location_cm[0], [double]$base.body.location_cm[1], [double]$base.body.location_cm[2]) }
$places = @{}
foreach ($entry in $stand.body.bodies) {
    $places[[string]$entry.bone] = @(($spawn[0] + [double]$entry.final_location_cm[0]), ($spawn[1] + [double]$entry.final_location_cm[1]), ($spawn[2] + [double]$entry.final_location_cm[2]))
}
foreach ($body in $bodyList) {
    if (-not $places.ContainsKey($body)) { Write-Output "GRAB PULLS ERROR: the character has no body '$body'"; exit 2 }
}

# Where a hand comes to rest against each body: a hand of 20 N sent to the body's middle.
$probes = @()
foreach ($body in $bodyList) {
    $middle = $places[$body]
    $side = 'front'
    if ($body -match '(arm|hand|clavicle)_l$') { $side = 'left' } elseif ($body -match '(arm|hand|clavicle)_r$') { $side = 'right' }
    $from = $ways[$side]
    $name = "grab_probe_$body"
    $file = Join-Path $scenarioDir "$name.json"
    $keys = @((New-Key 0.0 (Add-Vector $middle $from 45.0) 0), (New-Key 0.8 $middle 0))
    Write-JsonFile $file ([ordered]@{ name = $name; duration_s = 6.0; body = $base.body; events = @((New-HandEvent $keys)); expect = [ordered]@{ fell = $false } })
    $probes += [ordered]@{ body = $body; side = $side; from = $from; file = $file; dir = (Join-Path $Out "runs\$name") }
}
$probeJobs = @()
foreach ($probe in $probes) {
    $job = @{ File = $probe.file; Out = $probe.dir; Tune = (Join-Tune $Tune 'effectors.hand.max_force_n=20') }
    if ($CVars) { $job.CVars = $CVars }
    $probeJobs += $job
}
Invoke-ScenarioBatch -Jobs $probeJobs -Parallel $Parallel
foreach ($probe in $probes) {
    $run = Read-JsonFile (Join-Path $probe.dir 'metrics.json')
    $hand = $null
    if ($run -and $run.ok) { $hand = $run.effectors.list.hand_right }
    if (-not $hand -or [double]$hand.contact_frames -le 0) { Write-Output "GRAB PULLS ERROR: the probing hand did not come to rest against '$($probe.body)'"; exit 2 }
    $probe.surface = @([double]$hand.final_location_cm[0], [double]$hand.final_location_cm[1], [double]$hand.final_location_cm[2])
}

$trials = @()
foreach ($probe in $probes) {
    foreach ($way in $wayList) {
        $toward = $ways[$way]
        # A pull straight back into the body is a push.
        if (($toward[0] * $probe.from[0] + $toward[1] * $probe.from[1] + $toward[2] * $probe.from[2]) -lt -0.5) { continue }
        foreach ($speed in $speedList) {
            # The speed in tenths of a metre a second: 0.5 and 1 m/s must not share a name.
            $name = ('grab_{0}_{1}_{2:000}' -f $probe.body, $way, [int][Math]::Round($speed * 10.0))
            $touch = Add-Vector $probe.surface $probe.from (-$PressCm)
            $away = Add-Vector $touch $toward $DistanceCm
            $travelS = $DistanceCm / ($speed * 100.0)
            $keys = @(
                (New-Key 0.0 (Add-Vector $probe.surface $probe.from 45.0) 0),
                (New-Key 0.5 $touch 0),
                (New-Key 0.9 $touch 0),
                (New-Key 1.0 $touch 1),
                (New-Key (1.0 + $travelS) $away 1),
                (New-Key (1.0 + $travelS + $HoldS) $away 1),
                (New-Key (1.1 + $travelS + $HoldS) $away 0),
                (New-Key (1.6 + $travelS + $HoldS) (Add-Vector $away $probe.from 40.0) 0))
            $scenario = [ordered]@{
                name       = $name
                duration_s = [Math]::Round($startTime + 1.6 + $travelS + $HoldS + 3.0, 2)
                body       = $base.body
                events     = @([ordered]@{ t = 0.0; type = 'console'; command = 'Sandbox.Tune effectors.hand.grab=1' }, (New-HandEvent $keys))
                expect     = [ordered]@{
                    effector_grabs       = 1
                    max_grab_stretch_cm  = $MaxGrabStretchCm
                    max_joint_stretch_cm = $MaxJointStretchCm
                    max_grab_force_n     = $MaxGrabForceN
                    max_energy_gain_j    = $MaxEnergyGainJ
                }
            }
            $file = Join-Path $scenarioDir "$name.json"
            Write-JsonFile $file $scenario
            $trials += [ordered]@{ name = $name; body = $probe.body; way = $way; speed = $speed; file = $file; dir = (Join-Path $Out "runs\$name") }
        }
    }
}

if ($trials.Count -eq 0) { Write-Output 'GRAB PULLS ERROR: no pull is left: every direction asked for is a push for the bodies asked for'; exit 2 }
$jobs = @()
foreach ($trial in $trials) {
    $job = @{ File = $trial.file; Out = $trial.dir }
    if ($Tune) { $job.Tune = $Tune }
    if ($CVars) { $job.CVars = $CVars }
    $jobs += $job
}
Invoke-ScenarioBatch -Jobs $jobs -Parallel $Parallel
$seconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 1)

$results = @()
$missing = @()
foreach ($trial in $trials) {
    $run = Read-JsonFile (Join-Path $trial.dir 'metrics.json')
    if (-not $run -or -not $run.ok) { $missing += $trial.name; continue }
    $hand = $run.effectors.list.hand_right
    $failed = @($run.checks | Where-Object { -not $_.pass } | ForEach-Object { $_.name })
    if ([double]$hand.grabs -ge 1 -and [string]$hand.last_grabbed -ne $trial.body) { $failed += "took_$($hand.last_grabbed)" }
    if ($null -ne $hand.mean_grab_force_n -and [double]$hand.mean_grab_force_n -gt $MaxMeanGrabForceN) { $failed += 'mean_grab_force_n' }
    $bodyDid = 'stood'
    if ([bool]$run.metrics.fell) { $bodyDid = 'fell' } elseif ([double]$run.control.step_count -gt 0) { $bodyDid = "stepped $($run.control.step_count)" }
    $gripDid = 'held'
    if ([double]$hand.grabs -lt 1) { $gripDid = 'no grab' }
    elseif ([double]$hand.grab_overloads -gt 0) { $gripDid = 'torn' }
    elseif ([double]$hand.grab_slips -gt 0) { $gripDid = 'slipped' }
    elseif ([double]$hand.grabs_lost -gt 0) { $gripDid = 'lost' }
    $results += [ordered]@{
        name                = $trial.name
        body                = $trial.body
        way                 = $trial.way
        speed_mps           = $trial.speed
        grabbed             = [string]$hand.last_grabbed
        body_did            = $bodyDid
        end_state           = $run.control.end_state
        grip_did            = $gripDid
        grab_frames         = $hand.grab_frames
        grab_force_n        = $hand.max_grab_force_n
        grab_force_mean_n   = $hand.mean_grab_force_n
        grab_stretch_cm     = $hand.max_grab_stretch_cm
        joint_stretch_cm    = $run.metrics.max_joint_stretch_cm
        worst_joint         = $run.metrics.worst_joint
        joint_speed_rads    = $run.metrics.max_joint_speed_rads
        fastest_joint       = $run.metrics.fastest_joint
        limit_violation_deg = $run.metrics.max_joint_limit_violation_deg
        limit_joint         = $run.metrics.worst_limit_joint
        body_speed_mps      = $run.metrics.max_body_speed_mps
        energy_gain_j       = $run.energy.max_gain_j
        pelvis_drift_cm     = $run.metrics.max_pelvis_drift_cm
        hash                = $run.hashes.bodies
        failed_checks       = $failed
        pass                = ($failed.Count -eq 0)
    }
}
if ($missing.Count -gt 0) {
    Write-Output "GRAB PULLS ERROR: $($missing.Count) of $($trials.Count) run(s) produced no result, first: $($missing[0])"
    exit 2
}

$notPassed = @($results | Where-Object { -not $_.pass })
function Get-Largest($field) {
    return [double](($results | Sort-Object { [double]$_[$field] } -Descending | Select-Object -First 1)[$field])
}
$verdict = 'OK'
if ($notPassed.Count -gt 0) { $verdict = 'FAILED' }
$fellCount = @($results | Where-Object { $_.body_did -eq 'fell' }).Count
$slipCount = @($results | Where-Object { $_.grip_did -eq 'slipped' }).Count
$tornCount = @($results | Where-Object { $_.grip_did -eq 'torn' }).Count
Write-Output ("GRAB PULLS {0}: {1} of {2} pull(s) pass in {3} s; the grab's joint stretched {4:N3} cm at most and carried {5:N0} N on average at most ({6:N0} N in one frame), the body's joints {7:N2} cm, most energy gained {8:N1} J; {9} fell, {10} grip(s) slipped, {11} torn" -f `
    $verdict, ($results.Count - $notPassed.Count), $results.Count, $seconds, (Get-Largest 'grab_stretch_cm'), (Get-Largest 'grab_force_mean_n'), (Get-Largest 'grab_force_n'),
    (Get-Largest 'joint_stretch_cm'), (Get-Largest 'energy_gain_j'), $fellCount, $slipCount, $tornCount)
Write-Output ('  {0,-11} {1,-6} {2,4}  {3,-10} {4,-8} {5,-9} {6,8} {7,-16} {8,-20} {9,-18} {10,6} {11,6}  {12}' -f `
    'body', 'way', 'm/s', 'body', 'grip', 'N mean/max', 'grab cm', 'joints cm', 'fastest rad/s', 'past limit deg', 'm/s', 'gain J', 'verdict')
foreach ($r in $results) {
    $verdictText = 'pass'
    if (-not $r.pass) { $verdictText = 'FAIL: ' + ($r.failed_checks -join ', ') }
    Write-Output ('  {0,-11} {1,-6} {2,4:0.#}  {3,-10} {4,-8} {5,-9} {6,8:N3} {7,-16} {8,-20} {9,-18} {10,6:N1} {11,6:N1}  {12}' -f `
        $r.body, $r.way, $r.speed_mps, $r.body_did, $r.grip_did, ('{0:N0}/{1:N0}' -f [double]$r.grab_force_mean_n, [double]$r.grab_force_n), [double]$r.grab_stretch_cm,
        ('{0:N2} {1}' -f [double]$r.joint_stretch_cm, $r.worst_joint),
        ('{0:N1} {1}' -f [double]$r.joint_speed_rads, $r.fastest_joint),
        ('{0:N1} {1}' -f [double]$r.limit_violation_deg, $r.limit_joint),
        [double]$r.body_speed_mps, [double]$r.energy_gain_j, $verdictText)
}

$summary = [ordered]@{
    ok          = ($notPassed.Count -eq 0)
    runs        = $results.Count
    seconds     = $seconds
    bodies      = $bodyList
    speeds_mps  = $speedList
    directions  = $wayList
    distance_cm = $DistanceCm
    hold_s      = $HoldS
    press_cm    = $PressCm
    tune        = $Tune
    cvars       = $CVars
    limits      = [ordered]@{
        max_grab_stretch_cm = $MaxGrabStretchCm; max_joint_stretch_cm = $MaxJointStretchCm
        max_grab_force_n = $MaxGrabForceN; max_mean_grab_force_n = $MaxMeanGrabForceN; max_energy_gain_j = $MaxEnergyGainJ
    }
    surfaces    = @($probes | ForEach-Object { [ordered]@{ body = $_.body; from = $_.side; hand_rests_at_cm = (Format-Point $_.surface) } })
    results     = $results
}
$resultFile = Join-Path $Out 'grab_pulls.json'
Write-JsonFile $resultFile $summary
Write-Output "  result: $resultFile"

if ($notPassed.Count -gt 0) { exit 1 }
exit 0
