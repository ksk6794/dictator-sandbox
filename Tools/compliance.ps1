# How far a steady push moves the standing body: 20 N and 60 N for 1.5 s on eight bodies, from the front, the back
# and a side, and what stiffness that is.
#   powershell -ExecutionPolicy Bypass -File Tools\compliance.ps1
#   ... -Bodies "spine_04,hand_l" -Forces "20" -Ways "front,side"    a part of it
#   ... -PushS 1.5                how long the force is held
#   ... -Idle                     with the idle's motion on (it moves a hand by centimetres on its own; off unless asked for)
#   ... -Tune "name=value,..."    tuning overrides for every run
#   ... -Out <dir>                output directory (default Saved\Matrix\compliance)
#
# "front" is a push from the front (the body is moved back), "back" one from behind, "side" one from its left.
# Displacement is that of the pushed body's centre of mass along the push. "gave" is the furthest the push took it,
# and N/cm is the force over that. "against" is the furthest the body went against the push: it leans into one that is
# held (balance.lean_*). "held" is the mean over the last 0.2 s, "whole" the same of the centre of mass of all, and
# "local" the force over the difference of the two: what the joints between the place and the trunk give. A push that
# made the body step or fall is named, and its figures are not a stiffness.
#
# Prints one row a push, writes <Out>\compliance.json. Exit code: 0 every push was measured, 2 one was not.
param(
    [string]$Bodies = 'neck_01,spine_04,pelvis,upperarm_l,lowerarm_l,hand_l,thigh_l,calf_l',
    [string]$Forces = '20,60',
    [string]$Ways = 'front,back,side',
    [double]$PushS = 1.5,
    [int]$Parallel = 8,
    [switch]$Idle,
    [string]$Tune,
    [string]$Out
)
. "$PSScriptRoot\common.ps1"

if (-not $Out) { $Out = Join-Path $RepoRoot 'Saved\Matrix\compliance' }
$Out = [System.IO.Path]::GetFullPath($Out)
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
$scenarioDir = Join-Path $Out 'scenarios'
New-Item -ItemType Directory -Force $scenarioDir | Out-Null
$culture = [System.Globalization.CultureInfo]::InvariantCulture
$directions = @{ front = @(0, -1, 0); back = @(0, 1, 0); side = @(-1, 0, 0) }
$base = Read-JsonFile (Join-Path $RepoRoot 'Scenarios\stand_npc.json')
$pushAt = 3.0

$trials = @()
foreach ($body in @($Bodies.Split(',') | ForEach-Object { $_.Trim() })) {
    foreach ($way in @($Ways.Split(',') | ForEach-Object { $_.Trim() })) {
        if (-not $directions.ContainsKey($way)) { Write-Output "COMPLIANCE ERROR: '$way' is not front, back or side"; exit 2 }
        foreach ($force in @($Forces.Split(',') | ForEach-Object { [double]::Parse($_.Trim(), $culture) })) {
            $name = 'push_{0}_{1}_{2:000}' -f $body, $way, $force
            $scenario = [ordered]@{
                name = $name; duration_s = ($pushAt + $PushS + 3.0); body = $base.body
                events = @([ordered]@{ t = $pushAt; type = 'impulse'; body = $body; impulse_ns = ($force * $PushS); duration_s = $PushS; direction = $directions[$way] })
                expect = [ordered]@{}
            }
            $file = Join-Path $scenarioDir "$name.json"
            Write-JsonFile $file $scenario
            $trials += [ordered]@{ name = $name; body = $body; way = $way; force = $force; file = $file; dir = (Join-Path $Out "runs\$name") }
        }
    }
}

$allTune = $Tune
if (-not $Idle) { $allTune = (@('poses.idle.weight=0,poses.idle.sway_weight=0', $Tune) | Where-Object { $_ }) -join ',' }
$started = Get-Date
$jobs = @()
foreach ($trial in $trials) {
    $job = @{ File = $trial.file; Out = $trial.dir }
    if ($allTune) { $job.Tune = $allTune }
    $jobs += $job
}
Invoke-ScenarioBatch -Jobs $jobs -Parallel $Parallel
$seconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 1)

$results = @()
$missing = @()
foreach ($trial in $trials) {
    $run = Read-JsonFile (Join-Path $trial.dir 'metrics.json')
    $push = $null
    if ($run -and $run.ok) { $push = @($run.control.pushes)[0] }
    if (-not $push) { $missing += $trial.name; continue }
    # What the body did while it was pushed: later is another matter.
    $end = $pushAt + $PushS
    $states = @($run.control.states | Where-Object { $_.t -ge ($pushAt - 0.02) -and $_.t -le $end } | ForEach-Object { $_.state })
    $steps = @($run.control.recovery_steps | Where-Object { $_.start_t -ge ($pushAt - 0.02) -and $_.start_t -le $end }).Count
    $outcome = 'stood'
    if ($states -contains 'flinch') { $outcome = 'flinched' }
    if ($states -contains 'stagger' -or $steps -gt 0) { $outcome = 'stepped' }
    if ($states -contains 'falling') { $outcome = 'fell' }
    $results += [ordered]@{
        name = $trial.name; body = $trial.body; way = $trial.way; force_n = $trial.force; outcome = $outcome
        displacement_cm = $push.displacement_cm; peak_along_cm = $push.peak_along_cm; against_cm = $push.against_cm; peak_stiffness_n_per_cm = $push.peak_stiffness_n_per_cm; settled_cm = $push.settled_displacement_cm; whole_body_cm = $push.whole_body_cm
        return_s = $push.return_s; stiffness_n_per_cm = $push.stiffness_n_per_cm; local_stiffness_n_per_cm = $push.local_stiffness_n_per_cm
    }
}
if ($missing.Count -gt 0) {
    Write-Output "COMPLIANCE ERROR: $($missing.Count) of $($trials.Count) push(es) were not measured, first: $($missing[0])"
    exit 2
}
Write-Output "COMPLIANCE OK: $($results.Count) pushes measured in $seconds s"
Write-Output ('  {0,-12} {1,-6} {2,5} {3,8} {4,8} {5,11} {6,9} {7,9} {8,8} {9,8}  {10}' -f 'body', 'from', 'N', 'gave cm', 'N/cm', 'against cm', 'held cm', 'whole cm', 'local', 'back s', 'the body')
foreach ($r in $results) {
    $text = { param($value, $format) if ($null -eq $value) { return '-' }; return ([double]$value).ToString($format, $culture) }
    Write-Output ('  {0,-12} {1,-6} {2,5:N0} {3,8} {4,8} {5,11} {6,9} {7,9} {8,8} {9,8}  {10}' -f $r.body, $r.way, $r.force_n,
        (& $text $r.peak_along_cm 'F2'), (& $text $r.peak_stiffness_n_per_cm 'F1'), (& $text $r.against_cm 'F2'), (& $text $r.settled_cm 'F2'),
        (& $text $r.whole_body_cm 'F2'), (& $text $r.local_stiffness_n_per_cm 'F1'), (& $text $r.return_s 'F2'), $r.outcome)
}
$resultFile = Join-Path $Out 'compliance.json'
Write-JsonFile $resultFile ([ordered]@{ seconds = $seconds; push_s = $PushS; idle = [bool]$Idle; tune = $Tune; results = $results })
Write-Output "  result: $resultFile"
exit 0
