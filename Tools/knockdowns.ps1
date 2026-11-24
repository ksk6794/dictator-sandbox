# Knocks the standing NPC down from every direction and judges each fall by the criteria of M3 part 5
# (docs/PRD.md): the body falls, lies at rest in time, gains no energy beyond its muscles' work, does not sink
# into the floor or into itself, and no joint is pulled apart.
#   powershell -ExecutionPolicy Bypass -File Tools\knockdowns.ps1
#   ... -Impulses "200"                 newton-seconds, one row of directions for each
#   ... -Bodies "spine_04"              bones of the bodies struck
#   ... -Directions 8                   evenly spaced horizontal directions
#   ... -Tune "name=value,..."          tuning overrides for every run
#   ... -CVars "name=value,..."         console variables for every run (p.Chaos.Solver.UseCCD=0 to compare)
#   ... -Film                           also a sheet of pictures of every fall (Saved\Films\knockdown_<...>)
#   ... -Out <dir>                      output directory (default Saved\Matrix\knockdowns)
#
# A direction is the way the blow moves the body, as in Tools\push-matrix.ps1: 0 toward the character's left
# (+x), 90 forward (+y), 180 to its right, 270 backward.
#
# Limits (each a parameter): the body lies at rest within -MaxRestS of starting to fall; its energy rises by no
# more than -MaxEnergyGainJ (a falling body's muscles do work: they fold the legs and put the arms out);
# no body is deeper than -MaxPenetrationCm in the world or -MaxSelfPenetrationCm in another body of the
# character; no joint stretches more than -MaxJointStretchCm. A joint turned past its limit and the fastest
# joint are printed, and judged only when -MaxLimitViolationDeg or -MaxJointSpeedRadS is given.
#
# Prints one row per fall, writes <Out>\knockdowns.json. Exit code: 0 every fall passes, 1 one does not,
# 2 a run produced no result.
param(
    [string]$Impulses = '200',
    [string]$Bodies = 'spine_04',
    [int]$Directions = 8,
    [int]$Parallel = 8,
    [string]$Tune,
    [string]$CVars,
    [switch]$Film,
    [string]$Out,
    [double]$MaxRestS = 3.0,
    [double]$MaxEnergyGainJ = 40.0,
    [double]$MaxPenetrationCm = 2.0,
    [double]$MaxSelfPenetrationCm = 2.0,
    [double]$MaxJointStretchCm = 2.0,
    [double]$MaxLimitViolationDeg = 0,
    [double]$MaxJointSpeedRadS = 0
)
. "$PSScriptRoot\common.ps1"

if (-not $Out) { $Out = Join-Path $RepoRoot 'Saved\Matrix\knockdowns' }
$Out = [System.IO.Path]::GetFullPath($Out)
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
$scenarioDir = Join-Path $Out 'scenarios'
New-Item -ItemType Directory -Force $scenarioDir | Out-Null

$culture = [System.Globalization.CultureInfo]::InvariantCulture
$impulseList = @($Impulses.Split(',') | ForEach-Object { [double]::Parse($_.Trim(), $culture) } | Sort-Object)
$bodyList = @($Bodies.Split(',') | ForEach-Object { $_.Trim() })
$base = Read-JsonFile (Join-Path $RepoRoot 'Scenarios\stand_npc.json')
$hitTime = 3.0

$trials = @()
foreach ($body in $bodyList) {
    foreach ($impulse in $impulseList) {
        for ($d = 0; $d -lt $Directions; $d++) {
            $angle = 360.0 * $d / $Directions
            $name = ('knockdown_{0}_{1:000}_{2:000}' -f $body, $angle, $impulse)
            $radians = $angle * [Math]::PI / 180.0
            $scenario = [ordered]@{
                name       = $name
                duration_s = 10.0
                body       = $base.body
                events     = @([ordered]@{
                    t          = $hitTime
                    type       = 'impulse'
                    body       = $body
                    impulse_ns = $impulse
                    direction  = @([Math]::Round([Math]::Cos($radians), 6), [Math]::Round([Math]::Sin($radians), 6), 0)
                })
                expect     = [ordered]@{
                    fell                     = $true
                    end_state                = 'lying'
                    min_fall_time_s          = $hitTime
                    max_rest_after_fall_s    = $MaxRestS
                    max_energy_gain_j        = $MaxEnergyGainJ
                    max_body_penetration_cm  = $MaxPenetrationCm
                    max_self_penetration_cm  = $MaxSelfPenetrationCm
                    max_joint_stretch_cm     = $MaxJointStretchCm
                }
            }
            $file = Join-Path $scenarioDir "$name.json"
            Write-JsonFile $file $scenario
            $trials += [ordered]@{ name = $name; body = $body; angle = $angle; impulse = $impulse; file = $file; dir = (Join-Path $Out "runs\$name") }
        }
    }
}

$started = Get-Date
$jobs = @()
foreach ($trial in $trials) {
    $job = @{ File = $trial.file; Out = $trial.dir }
    if ($Tune) { $job.Tune = $Tune }
    if ($CVars) { $job.CVars = $CVars }
    $jobs += $job
}
Invoke-ScenarioBatch -Jobs $jobs -Parallel $Parallel
$seconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 1)

function Get-FallWay($runDir) {
    # The pose the body took, as the controller chose it when the fall began.
    $log = Join-Path $runDir 'run.log'
    if (-not (Test-Path $log)) { return '' }
    $line = Get-Content $log | Where-Object { $_ -match 'going down ' } | Select-Object -First 1
    if ($line -and $line -match 'going down (?:to the )?(\w+)') { return $Matches[1] }
    return ''
}

$results = @()
$missing = @()
foreach ($trial in $trials) {
    $run = Read-JsonFile (Join-Path $trial.dir 'metrics.json')
    if (-not $run -or -not $run.ok) { $missing += $trial.name; continue }
    $failed = @($run.checks | Where-Object { -not $_.pass } | ForEach-Object { $_.name })
    if ($MaxLimitViolationDeg -gt 0 -and $run.metrics.max_joint_limit_violation_deg -gt $MaxLimitViolationDeg) { $failed += 'max_joint_limit_violation_deg' }
    if ($MaxJointSpeedRadS -gt 0 -and $run.metrics.max_joint_speed_rads -gt $MaxJointSpeedRadS) { $failed += 'max_joint_speed_rads' }
    $results += [ordered]@{
        name                 = $trial.name
        body                 = $trial.body
        angle                = $trial.angle
        impulse_ns           = $trial.impulse
        fell                 = [bool]$run.metrics.fell
        end_state            = $run.control.end_state
        state_sequence       = $run.control.state_sequence
        way                  = (Get-FallWay $trial.dir)
        steps                = $run.control.recovery_step_count
        fall_started_t       = $run.control.fall.started_t
        rest_after_s         = $run.control.fall.rest_after_s
        energy_gain_j        = $run.energy.max_gain_j
        energy_gain_t        = $run.energy.max_gain_t
        penetration_cm       = $run.penetration.max_world_cm
        penetration_bone     = $run.penetration.world_bone
        self_penetration_cm  = $run.penetration.max_self_cm
        self_bones           = ('{0}/{1}' -f $run.penetration.self_bone_a, $run.penetration.self_bone_b)
        joint_stretch_cm     = $run.metrics.max_joint_stretch_cm
        limit_violation_deg  = $run.metrics.max_joint_limit_violation_deg
        limit_joint          = $run.metrics.worst_limit_joint
        joint_speed_rads     = $run.metrics.max_joint_speed_rads
        fastest_joint        = $run.metrics.fastest_joint
        physics_step_ms      = $run.physics_step_ms.mean
        hash                 = $run.hashes.bodies
        failed_checks        = $failed
        pass                 = ($failed.Count -eq 0)
    }
}
if ($missing.Count -gt 0) {
    Write-Output "KNOCKDOWNS ERROR: $($missing.Count) of $($trials.Count) run(s) produced no result, first: $($missing[0])"
    exit 2
}

$notPassed = @($results | Where-Object { -not $_.pass })
$worst = {
    param($field)
    $top = $results | Sort-Object { [double]$_[$field] } -Descending | Select-Object -First 1
    return $top
}
$verdict = 'OK'
if ($notPassed.Count -gt 0) { $verdict = 'FAILED' }
Write-Output ("KNOCKDOWNS {0}: {1} of {2} fall(s) pass in {3} s; slowest to rest {4:N2} s, most energy gained {5:N1} J, deepest in the floor {6:N2} cm, deepest in itself {7:N2} cm" -f `
    $verdict, ($results.Count - $notPassed.Count), $results.Count, $seconds,
    [double](& $worst 'rest_after_s').rest_after_s, [double](& $worst 'energy_gain_j').energy_gain_j,
    [double](& $worst 'penetration_cm').penetration_cm, [double](& $worst 'self_penetration_cm').self_penetration_cm)
Write-Output ('  {0,-10} {1,5} {2,5}  {3,-8} {4,5} {5,6} {6,7} {7,-18} {8,-22} {9,-20} {10,-16} {11}' -f `
    'body', 'angle', 'N*s', 'way', 'steps', 'rest s', 'gain J', 'in floor cm', 'in itself cm', 'past limit deg', 'fastest rad/s', 'verdict')
foreach ($r in $results) {
    $restText = '-'
    if ($null -ne $r.rest_after_s) { $restText = ('{0:N2}' -f [double]$r.rest_after_s) }
    $verdictText = 'pass'
    if (-not $r.pass) { $verdictText = 'FAIL: ' + ($r.failed_checks -join ', ') }
    Write-Output ('  {0,-10} {1,5:N0} {2,5:N0}  {3,-8} {4,5} {5,6} {6,7:N1} {7,-18} {8,-22} {9,-20} {10,-16} {11}' -f `
        $r.body, $r.angle, $r.impulse_ns, $r.way, $r.steps, $restText, [double]$r.energy_gain_j,
        ('{0:N2} {1}' -f [double]$r.penetration_cm, $r.penetration_bone),
        ('{0:N2} {1}' -f [double]$r.self_penetration_cm, $r.self_bones),
        ('{0:N1} {1}' -f [double]$r.limit_violation_deg, $r.limit_joint),
        ('{0:N1} {1}' -f [double]$r.joint_speed_rads, $r.fastest_joint), $verdictText)
}

$summary = [ordered]@{
    ok          = ($notPassed.Count -eq 0)
    runs        = $results.Count
    seconds     = $seconds
    impulses_ns = $impulseList
    bodies      = $bodyList
    directions  = $Directions
    tune        = $Tune
    cvars       = $CVars
    limits      = [ordered]@{
        max_rest_s = $MaxRestS; max_energy_gain_j = $MaxEnergyGainJ; max_penetration_cm = $MaxPenetrationCm
        max_self_penetration_cm = $MaxSelfPenetrationCm; max_joint_stretch_cm = $MaxJointStretchCm
        max_limit_violation_deg = $MaxLimitViolationDeg; max_joint_speed_rads = $MaxJointSpeedRadS
    }
    results     = $results
}
$resultFile = Join-Path $Out 'knockdowns.json'
Write-JsonFile $resultFile $summary
Write-Output "  result: $resultFile"

if ($Film) {
    # One sheet per fall, from the side the fall is best seen from.
    $filmScript = Join-Path $PSScriptRoot 'film.ps1'
    foreach ($trial in $trials) {
        $camera = 'side'
        if (($trial.angle -lt 45.0) -or ($trial.angle -gt 135.0 -and $trial.angle -lt 225.0) -or ($trial.angle -gt 315.0)) { $camera = 'front' }
        # The film tool takes a scenario of the suite and other events for it: the standing scenario with this fall's blow.
        $eventsFile = Join-Path $scenarioDir "$($trial.name).events.json"
        $blow = (Read-JsonFile $trial.file).events
        [System.IO.File]::WriteAllText($eventsFile, (ConvertTo-Json @($blow) -Depth 6))
        $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $filmScript, '-Name', 'stand_npc', '-Tag', $trial.name,
            '-Events', $eventsFile, '-Times', '3.0,3.1,3.2,3.3,3.4,3.5,3.7,3.9,4.2,4.6,5.2,6.0', '-Camera', $camera)
        if ($Tune) { $arguments += @('-Tune', $Tune) }
        & powershell @arguments | Select-Object -Last 1
    }
}

if ($notPassed.Count -gt 0) { exit 1 }
exit 0
