# Pushes the standing NPC with every combination of impulse, body and direction, several runs at a time,
# names the outcome of each push and judges it against the owner's outcome table of M3 (docs/PRD.md).
#   powershell -ExecutionPolicy Bypass -File Tools\push-matrix.ps1
#   ... -Impulses "10,30,80,200"                             newton-seconds
#   ... -Bodies "neck_01,spine_04,pelvis,thigh_l,thigh_r"    bones of the bodies to push
#   ... -Directions 8                                        evenly spaced horizontal directions
#   ... -PushSeconds 0.1                                     time over which each impulse is delivered; 0 is a single blow
#   ... -Enforce "10,30,80,200"                                impulses whose rows are judged; the others are reported only
#   ... -Rules m2                                            the M2 gate instead: who fell, nothing else
#   ... -Parallel 8                                          scenario processes at a time
#   ... -Tune "name=value,..."                               tuning overrides for every run
#   ... -StepMinCm 10                                        how far a foot has to travel in the air to count as a step
#   ... -Out <dir>                                           output directory (default Saved\Matrix\push)
#
# A direction is the way the push moves the body, as an angle in the floor plane: 0 is toward the
# character's left (+x), 90 forward (+y), 180 to its right, 270 backward. "Backward" in the table is 225, 270
# and 315; the other five directions are forward and sideways.
#
# The outcome of a push, from the run's own result:
#   fell     the body fell (metrics.fell)
#   stepped  it stayed up and made at least one step: a foot that left the floor and stood again -StepMinCm (10 cm)
#            or more away (control.steps), less the steps that only brought the feet back together afterwards
#            (control.recovery_steps[].regroup); a step the controller made counts whatever its length. A foot that
#            hops less than that is a shuffle: counted for each row, judged by nothing. With 5 cm, the run's own
#            least, one recovery step read as two wherever the other foot hopped 5 or 6 cm as the first landed
#   held     it stayed up without a step although its capture point needed one (control.balance.step_needed_frames):
#            the balance assist held it. Until the recovery step exists (M3 part 4) this is where a step would be.
#   stood    it stayed up without a step and never needed one
#
# The table (owner's decision after M3 part 1), with the M2 rules for the smallest and the largest impulse:
#   10 N*s   stood
#   30 N*s   never a fall; forward and sideways: stood; backward, and any push on the head: stood or one step
#   80 N*s   trunk (spine_04, pelvis): stepped, and backward also fell; head and thighs: stepped or fell; anywhere
#            stood if a step was never needed (the capture point stayed within reach of the ankles)
#   200 N*s  chest and head: fell; pelvis and thighs: fell or stepped (the body runs the push off)
# "held" is a failure in every cell: the assist held a body that needed a step.
# Mirror-image pushes (left thigh from the left against right thigh from the right) that end differently are listed
# and counted, not judged (owner's decision during part 4): where the table allows one outcome a difference breaks the
# table anyway, and where it allows a step or a fall, or a capture point lands at the threshold of a step, the outcome
# is chaotic and a mirror rule judged luck. Every run must pass its own checks (no NaN, joint stretch within the limit).
#
# Prints one row per body and direction, writes <Out>\matrix.json. Exit code: 0 no judged rule is broken,
# 1 a rule is broken, 2 a run produced no result.
param(
    [string]$Impulses = '10,30,80,200',
    [string]$Bodies = 'neck_01,spine_04,pelvis,thigh_l,thigh_r',
    [int]$Directions = 8,
    [double]$PushSeconds = 0,
    [string]$Enforce = '10,30,80,200',
    [ValidateSet('m3', 'm2')][string]$Rules = 'm3',
    [int]$Parallel = 8,
    [string]$Tune,
    [string]$Out,
    [double]$MaxJointStretchCm = 2.0,
    [double]$StepMinCm = 10.0
)
. "$PSScriptRoot\common.ps1"

if (-not $Out) { $Out = Join-Path $RepoRoot 'Saved\Matrix\push' }
$Out = [System.IO.Path]::GetFullPath($Out)
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
$scenarioDir = Join-Path $Out 'scenarios'
New-Item -ItemType Directory -Force $scenarioDir | Out-Null

$culture = [System.Globalization.CultureInfo]::InvariantCulture
$impulseList = @($Impulses.Split(',') | ForEach-Object { [double]::Parse($_.Trim(), $culture) } | Sort-Object)
$enforced = @($Enforce.Split(',') | Where-Object { $_.Trim() } | ForEach-Object { [double]::Parse($_.Trim(), $culture) })
$bodyList = @($Bodies.Split(',') | ForEach-Object { $_.Trim() })
$base = Read-JsonFile (Join-Path $RepoRoot 'Scenarios\stand_npc.json')
$pushTime = 2.0

function Get-MirrorBody([string]$Body) {
    if ($Body.EndsWith('_l')) { return $Body.Substring(0, $Body.Length - 2) + '_r' }
    if ($Body.EndsWith('_r')) { return $Body.Substring(0, $Body.Length - 2) + '_l' }
    return $Body
}

function Test-Backward([double]$Angle) { return ($Angle -gt 200.0 -and $Angle -lt 340.0) }

# The outcomes the table allows for one push, and whether a "held" there stands for a step to come.
function Get-Allowed([string]$Body, [double]$Angle, [double]$Impulse) {
    $isHead = ($Body -eq 'neck_01' -or $Body -eq 'head')
    $isTrunk = ($Body -eq 'spine_04' -or $Body -eq 'pelvis' -or $Body -like 'spine_*')
    $backward = Test-Backward $Angle
    if ($Impulse -le 10) { return @{ classes = @('stood'); max_steps = 0 } }
    if ($Impulse -le 30) {
        if ($backward -or $isHead) { return @{ classes = @('stood', 'stepped'); max_steps = 1 } }
        return @{ classes = @('stood'); max_steps = 0 }
    }
    if ($Impulse -le 80) {
        # Standing without a step is allowed only if a step was never needed ("stood"): a body that needed one and
        # stood all the same ("held") was held by the assist (owner's decision during part 4).
        if ($isTrunk -and -not $backward) { return @{ classes = @('stood', 'stepped'); max_steps = 99 } }
        return @{ classes = @('stood', 'stepped', 'fell'); max_steps = 99 }
    }
    # A push near the centre of mass sends the body off without turning it over: it may run the push off (owner's
    # go-ahead for the audit of November 2026: the body always tries to step). On the chest or the head it falls.
    if ($Body -eq 'pelvis' -or $Body -like 'thigh_*') { return @{ classes = @('stepped', 'fell'); max_steps = 99 } }
    return @{ classes = @('fell'); max_steps = 0 }
}

# One trial per impulse, body and direction.
$trials = @()
foreach ($body in $bodyList) {
    for ($d = 0; $d -lt $Directions; $d++) {
        $angle = 360.0 * $d / $Directions
        foreach ($impulse in $impulseList) {
            $name = ('push_{0}_{1:000}_{2:000}' -f $body, $angle, $impulse)
            $radians = $angle * [Math]::PI / 180.0
            $event = [ordered]@{
                t          = $pushTime
                type       = 'impulse'
                body       = $body
                impulse_ns = $impulse
                direction  = @([Math]::Round([Math]::Cos($radians), 6), [Math]::Round([Math]::Sin($radians), 6), 0)
            }
            if ($PushSeconds -gt 0) { $event.duration_s = $PushSeconds }
            $scenario = [ordered]@{
                name       = $name
                duration_s = 8.0
                body       = $base.body
                events     = @($event)
                expect     = [ordered]@{ max_joint_stretch_cm = $MaxJointStretchCm }
            }
            $file = Join-Path $scenarioDir "$name.json"
            Write-JsonFile $file $scenario
            $trials += [ordered]@{ name = $name; body = $body; angle = $angle; impulse = $impulse; file = $file; dir = (Join-Path $Out "runs\$name") }
        }
    }
}

# Run them, $Parallel at a time. Every scenario process is independent and uses a fixed timestep, so
# running side by side does not change any result.
$started = Get-Date
$jobs = @()
foreach ($trial in $trials) {
    $job = @{ File = $trial.file; Out = $trial.dir }
    if ($Tune) { $job.Tune = $Tune }
    $jobs += $job
}
Invoke-ScenarioBatch -Jobs $jobs -Parallel $Parallel
$seconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 1)

# Collect.
$results = @()
$missing = @()
foreach ($trial in $trials) {
    $runMetrics = Read-JsonFile (Join-Path $trial.dir 'metrics.json')
    if (-not $runMetrics -or -not $runMetrics.ok) { $missing += $trial.name; continue }
    $failedChecks = @($runMetrics.checks | Where-Object { -not $_.pass } | ForEach-Object { $_.name })
    $fell = [bool]$runMetrics.metrics.fell
    # Steps, measured from outside: a foot that left the floor and stood again -StepMinCm or more away. The run's
    # own record starts at 5 cm, and a foot that hops 5 or 6 cm as the other one lands is a shuffle, not a step
    # (owner's decision, 24 November 2026): shuffles are counted beside the steps and judged by nothing.
    $flights = @($runMetrics.control.steps | Where-Object { $null -ne $_ })
    $stepCount = @($flights | Where-Object { [double]$_.length_cm -ge $StepMinCm }).Count
    $shuffleCount = $flights.Count - $stepCount
    # A step that only brought the feet back together after the body had its balance again is not a step the push
    # cost: the controller's own record names those (control.recovery_steps[].regroup).
    $own = @($runMetrics.control.recovery_steps | Where-Object { $null -ne $_ })
    $regroupLong = @($own | Where-Object { $_.regroup -and [double]$_.landed_length_cm -ge $StepMinCm }).Count
    $ownCount = @($own | Where-Object { -not $_.regroup }).Count
    # A step the controller made counts whatever its length: a body that answered with a short step was not held.
    $stepCount = [Math]::Max([Math]::Max($stepCount - $regroupLong, 0), $ownCount)
    $stepNeeded = 0
    if ($null -ne $runMetrics.control.balance.step_needed_frames) { $stepNeeded = [int]$runMetrics.control.balance.step_needed_frames }
    $outcome = 'stood'
    if ($fell) { $outcome = 'fell' }
    elseif ($stepCount -gt 0) { $outcome = 'stepped' }
    elseif ($stepNeeded -gt 0) { $outcome = 'held' }
    $results += [ordered]@{
        name                  = $trial.name
        body                  = $trial.body
        angle                 = $trial.angle
        impulse_ns            = $trial.impulse
        outcome               = $outcome
        fell                  = $fell
        steps                 = $stepCount
        shuffles              = $shuffleCount
        step_needed_frames    = $stepNeeded
        fall_time_s           = $runMetrics.metrics.fall_time_s
        end_state             = $runMetrics.control.end_state
        state_sequence        = $runMetrics.control.state_sequence
        fall_reason           = (@($runMetrics.control.states | Where-Object { $_.state -eq 'falling' } | ForEach-Object { $_.reason }) | Select-Object -First 1)
        capture_margin_min_cm = $runMetrics.control.balance.capture_margin_min_cm
        assist_force_share_max = $runMetrics.control.assist_load.force_share_max
        recovery_steps        = $runMetrics.control.recovery_step_count
        stagger_s             = $runMetrics.control.stagger.seconds
        stagger_torque_share_max = $runMetrics.control.stagger.assist_torque_share_max
        max_joint_stretch_cm  = [Math]::Round($runMetrics.metrics.max_joint_stretch_cm, 3)
        max_joint_speed_rads  = [Math]::Round($runMetrics.metrics.max_joint_speed_rads, 1)
        max_pelvis_drift_cm   = [Math]::Round($runMetrics.metrics.max_pelvis_drift_cm, 1)
        failed_checks         = $failedChecks
    }
}
if ($missing.Count -gt 0) {
    Write-Output "PUSH MATRIX ERROR: $($missing.Count) of $($trials.Count) run(s) produced no result, first: $($missing[0])"
    exit 2
}

# Judge.
$byKey = @{}
foreach ($result in $results) { $byKey["$($result.body)|$($result.angle)|$($result.impulse_ns)"] = $result }
$smallest = $impulseList[0]
$largest = $impulseList[$impulseList.Count - 1]
$broken = @()
$notes = @()
$mirrors = @()
foreach ($result in $results) {
    $judged = $enforced -contains $result.impulse_ns
    $verdict = 'pass'
    if ($Rules -eq 'm2') {
        if (($result.impulse_ns -eq $smallest -and $result.fell) -or ($result.impulse_ns -eq $largest -and -not $result.fell)) { $verdict = 'fail' }
    }
    else {
        $allowed = Get-Allowed $result.body $result.angle $result.impulse_ns
        if ($result.outcome -eq 'held') {
            $verdict = 'fail'
        }
        elseif ($allowed.classes -notcontains $result.outcome) { $verdict = 'fail' }
        elseif ($result.outcome -eq 'stepped' -and $result.steps -gt $allowed.max_steps) { $verdict = 'fail' }
        $result.allowed = ($allowed.classes -join '|')
    }
    $result.verdict = $verdict
    $result.judged = $judged
    if ($verdict -eq 'fail') {
        $line = "$($result.name): $($result.outcome) (steps $($result.steps)), the table allows $($result.allowed)"
        if ($judged) { $broken += $line } else { $notes += $line }
    }
    if ($result.failed_checks.Count -gt 0) { $broken += "$($result.name): failed $($result.failed_checks -join ', ')" }

    # The mirror image of a push: the other side's body, the direction reflected in the body's midplane.
    $mirrorAngle = (540.0 - $result.angle) % 360.0
    $mirror = $byKey["$(Get-MirrorBody $result.body)|$mirrorAngle|$($result.impulse_ns)"]
    if ($mirror -and $mirror.name -gt $result.name -and $mirror.outcome -ne $result.outcome) {
        $mirrors += "$($result.name) and $($mirror.name): mirror-image pushes end differently ($($result.outcome) and $($mirror.outcome))"
    }
}

$counts = [ordered]@{}
foreach ($impulse in $impulseList) {
    $row = @($results | Where-Object { $_.impulse_ns -eq $impulse })
    $counts["$impulse"] = [ordered]@{
        judged  = ($enforced -contains $impulse)
        stood   = @($row | Where-Object { $_.outcome -eq 'stood' }).Count
        held    = @($row | Where-Object { $_.outcome -eq 'held' }).Count
        stepped = @($row | Where-Object { $_.outcome -eq 'stepped' }).Count
        fell    = @($row | Where-Object { $_.outcome -eq 'fell' }).Count
        pass    = @($row | Where-Object { $_.verdict -eq 'pass' }).Count
        pending = @($row | Where-Object { $_.verdict -eq 'pending' }).Count
        fail    = @($row | Where-Object { $_.verdict -eq 'fail' }).Count
        shuffles = ($row | ForEach-Object { [int]$_.shuffles } | Measure-Object -Sum).Sum
    }
}

Write-JsonFile (Join-Path $Out 'matrix.json') ([ordered]@{
    ok           = ($broken.Count -eq 0)
    rules        = $Rules
    enforced_ns  = $enforced
    runs         = $results.Count
    seconds      = $seconds
    impulses_ns  = $impulseList
    bodies       = $bodyList
    directions   = $Directions
    push_seconds = $PushSeconds
    tune         = $Tune
    step_min_cm  = $StepMinCm
    counts       = $counts
    broken_rules = $broken
    notes        = $notes
    mirror_differences = $mirrors
    results      = $results
})

# Table: one row per body and direction, one column per impulse. s stood, h held, S stepped (with the count),
# F fell; ! a cell the table does not allow, ? pending the recovery step.
$worstStretch = ($results | ForEach-Object { $_.max_joint_stretch_cm } | Measure-Object -Maximum).Maximum
$worstSpeed = ($results | ForEach-Object { $_.max_joint_speed_rads } | Measure-Object -Maximum).Maximum
$verdictText = 'OK'
if ($broken.Count -gt 0) { $verdictText = 'FAILED' }
Write-Output ("PUSH MATRIX {0}: {1} runs in {2} s, rules {3} judged at {4} N*s, push over {5} s, worst joint stretch {6} cm, fastest joint {7} rad/s" -f $verdictText, $results.Count, $seconds, $Rules, ($enforced -join '/'), $PushSeconds, $worstStretch, $worstSpeed)
Write-Output ('  {0,-12} {1,5}   {2}' -f 'body', 'angle', (($impulseList | ForEach-Object { '{0,6}' -f $_ }) -join ' '))
foreach ($body in $bodyList) {
    for ($d = 0; $d -lt $Directions; $d++) {
        $angle = 360.0 * $d / $Directions
        $cells = @()
        foreach ($impulse in $impulseList) {
            $cell = $byKey["$body|$angle|$impulse"]
            $mark = switch ($cell.outcome) { 'fell' { 'F' } 'stepped' { "S$($cell.steps)" } 'held' { 'h' } default { 's' } }
            if ($cell.verdict -eq 'fail') { $mark += '!' } elseif ($cell.verdict -eq 'pending') { $mark += '?' }
            $cells += ('{0,6}' -f $mark)
        }
        Write-Output ('  {0,-12} {1,5}   {2}' -f $body, $angle, ($cells -join ' '))
    }
}
foreach ($impulse in $impulseList) {
    $c = $counts["$impulse"]
    $judgedText = 'reported only'
    if ($c.judged) { $judgedText = 'judged' }
    Write-Output ("  {0,5} N*s ({1}): stood {2}, held {3}, stepped {4}, fell {5}; pass {6}, pending {7}, fail {8}; shuffles under {10:N0} cm {9}" -f $impulse, $judgedText, $c.stood, $c.held, $c.stepped, $c.fell, $c.pass, $c.pending, $c.fail, $c.shuffles, $StepMinCm)
}
foreach ($line in $broken) { Write-Output "  BROKEN: $line" }
Write-Output ("  mirror-image pairs that end differently: {0} (reported, not judged)" -f $mirrors.Count)
foreach ($line in $mirrors) { Write-Output "  MIRROR: $line" }
Write-Output "  result: $(Join-Path $Out 'matrix.json')"

if ($broken.Count -eq 0) { exit 0 }
exit 1
