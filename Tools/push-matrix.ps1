# Pushes the standing NPC with every combination of impulse, body and direction, several runs at a time,
# and checks the outcomes against the M2 rules.
#   powershell -ExecutionPolicy Bypass -File Tools\push-matrix.ps1
#   ... -Impulses "10,30,80,200"                      newton-seconds
#   ... -Bodies "neck_01,spine_04,thigh_l,thigh_r"    bones of the bodies to push
#   ... -Directions 8                                 evenly spaced horizontal directions
#   ... -PushSeconds 0.1                              time over which each impulse is delivered; 0 is a single blow
#   ... -Parallel 8                                   scenario processes at a time
#   ... -Tune "name=value,..."                        tuning overrides for every run
#   ... -Out <dir>                                    output directory (default Saved\Matrix\push)
#
# A direction is the way the push moves the body, as an angle in the floor plane: 0 is toward the
# character's left (+x), 90 forward (+y), 180 to its right, 270 backward.
#
# Rules checked (docs/PRD.md, M2):
#   - no push of the smallest impulse makes the character fall
#   - every push of the largest impulse does
#   - mirror-image pushes end the same way (left thigh from the left against right thigh from the right)
#   - no run fails its own checks (no NaN, joint stretch within the limit)
#
# Prints one row per body and direction, writes <Out>\matrix.json. Exit code: 0 all rules hold, 1 a rule is
# broken, 2 a run produced no result.
param(
    [string]$Impulses = '10,30,80,200',
    [string]$Bodies = 'neck_01,spine_04,thigh_l,thigh_r',
    [int]$Directions = 8,
    [double]$PushSeconds = 0,
    [int]$Parallel = 8,
    [string]$Tune,
    [string]$Out,
    [double]$MaxJointStretchCm = 2.0
)
. "$PSScriptRoot\common.ps1"

if (-not $Out) { $Out = Join-Path $RepoRoot 'Saved\Matrix\push' }
$Out = [System.IO.Path]::GetFullPath($Out)
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
$scenarioDir = Join-Path $Out 'scenarios'
New-Item -ItemType Directory -Force $scenarioDir | Out-Null

$culture = [System.Globalization.CultureInfo]::InvariantCulture
$impulseList = @($Impulses.Split(',') | ForEach-Object { [double]::Parse($_.Trim(), $culture) } | Sort-Object)
$bodyList = @($Bodies.Split(',') | ForEach-Object { $_.Trim() })
$base = Read-JsonFile (Join-Path $RepoRoot 'Scenarios\stand_npc.json')
$pushTime = 2.0

function Get-MirrorBody([string]$Body) {
    if ($Body.EndsWith('_l')) { return $Body.Substring(0, $Body.Length - 2) + '_r' }
    if ($Body.EndsWith('_r')) { return $Body.Substring(0, $Body.Length - 2) + '_l' }
    return $Body
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
    $metrics = Read-JsonFile (Join-Path $trial.dir 'metrics.json')
    if (-not $metrics -or -not $metrics.ok) { $missing += $trial.name; continue }
    $failedChecks = @($metrics.checks | Where-Object { -not $_.pass } | ForEach-Object { $_.name })
    $results += [ordered]@{
        name                 = $trial.name
        body                 = $trial.body
        angle                = $trial.angle
        impulse_ns           = $trial.impulse
        fell                 = [bool]$metrics.metrics.fell
        fall_time_s          = $metrics.metrics.fall_time_s
        end_state            = $metrics.control.end_state
        state_sequence       = $metrics.control.state_sequence
        max_joint_stretch_cm = [Math]::Round($metrics.metrics.max_joint_stretch_cm, 3)
        max_joint_speed_rads = [Math]::Round($metrics.metrics.max_joint_speed_rads, 1)
        max_pelvis_drift_cm  = [Math]::Round($metrics.metrics.max_pelvis_drift_cm, 1)
        failed_checks        = $failedChecks
    }
}
if ($missing.Count -gt 0) {
    Write-Output "PUSH MATRIX ERROR: $($missing.Count) of $($trials.Count) run(s) produced no result, first: $($missing[0])"
    exit 2
}

# Rules.
$byKey = @{}
foreach ($result in $results) { $byKey["$($result.body)|$($result.angle)|$($result.impulse_ns)"] = $result }
$smallest = $impulseList[0]
$largest = $impulseList[$impulseList.Count - 1]
$broken = @()
foreach ($result in $results) {
    if ($result.impulse_ns -eq $smallest -and $result.fell) { $broken += "$($result.name): fell from the smallest impulse" }
    if ($result.impulse_ns -eq $largest -and -not $result.fell) { $broken += "$($result.name): did not fall from the largest impulse" }
    if ($result.failed_checks.Count -gt 0) { $broken += "$($result.name): failed $($result.failed_checks -join ', ')" }

    # The mirror image of a push: the other side's body, the direction reflected in the body's midplane.
    $mirrorAngle = (540.0 - $result.angle) % 360.0
    $mirror = $byKey["$(Get-MirrorBody $result.body)|$mirrorAngle|$($result.impulse_ns)"]
    if ($mirror -and $mirror.name -gt $result.name -and $mirror.fell -ne $result.fell) {
        $broken += "$($result.name) and $($mirror.name): mirror-image pushes end differently (fell: $($result.fell) and $($mirror.fell))"
    }
}

Write-JsonFile (Join-Path $Out 'matrix.json') ([ordered]@{
    ok           = ($broken.Count -eq 0)
    runs         = $results.Count
    seconds      = $seconds
    impulses_ns  = $impulseList
    bodies       = $bodyList
    directions   = $Directions
    push_seconds = $PushSeconds
    tune         = $Tune
    broken_rules = $broken
    results      = $results
})

# Table: one row per body and direction, one column per impulse. F = fell, s = stood.
$worstStretch = ($results | ForEach-Object { $_.max_joint_stretch_cm } | Measure-Object -Maximum).Maximum
$worstSpeed = ($results | ForEach-Object { $_.max_joint_speed_rads } | Measure-Object -Maximum).Maximum
$verdict = 'OK'
if ($broken.Count -gt 0) { $verdict = 'FAILED' }
Write-Output ("PUSH MATRIX {0}: {1} runs in {2} s, push over {3} s, worst joint stretch {4} cm, fastest joint {5} rad/s" -f $verdict, $results.Count, $seconds, $PushSeconds, $worstStretch, $worstSpeed)
Write-Output ('  {0,-12} {1,5}   {2}' -f 'body', 'angle', (($impulseList | ForEach-Object { '{0,5}' -f $_ }) -join ' '))
foreach ($body in $bodyList) {
    for ($d = 0; $d -lt $Directions; $d++) {
        $angle = 360.0 * $d / $Directions
        $cells = @()
        foreach ($impulse in $impulseList) {
            $mark = 's'
            if ($byKey["$body|$angle|$impulse"].fell) { $mark = 'F' }
            $cells += ('{0,5}' -f $mark)
        }
        Write-Output ('  {0,-12} {1,5}   {2}' -f $body, $angle, ($cells -join ' '))
    }
}
foreach ($line in $broken) { Write-Output "  BROKEN: $line" }
Write-Output "  result: $(Join-Path $Out 'matrix.json')"

if ($broken.Count -eq 0) { exit 0 }
exit 1
