# The stand of M3 part 6: knocks the NPC down many times, each time differently, and counts how often it is back
# on its feet. The criterion of the part: it gets up in 90% of 50 falls (docs/PRD.md).
#   powershell -ExecutionPolicy Bypass -File Tools\getup-stand.ps1
#   ... -Seed 1                 the trials are drawn from this seed: the same seed, the same 50 falls
#   ... -Floor 26 -Wall 8 -Table 8 -Chair 8     how many falls of each kind
#   ... -Only "wall,chair"      only these kinds
#   ... -DurationS 40           simulated seconds of each trial (the blow comes at 3 s)
#   ... -MinShare 0.9           the share of falls that must end standing
#   ... -Tune "name=value,..."  tuning overrides for every run
#   ... -Film failed            also a sheet of pictures of every trial that did not end standing ("all": of every
#                               trial; Saved\Films\getup_stand_<n>)
#   ... -Out <dir>              output directory (default Saved\Matrix\getup_stand)
#
# The kinds. "floor": on the bare floor, a blow or a shove of 200 to 320 N*s on the chest or the head from any
# direction. "wall", "table", "chair": in the room of Specs\rooms\getup_room.json, the body standing 45 to 90 cm
# from a wall, the table or the chair and knocked toward it, within 50 degrees. The furniture is static.
# A direction is the way the blow moves the body, as in Tools\push-matrix.ps1: 0 toward the character's left
# (+x), 90 forward (+y), 180 to its right, 270 backward.
#
# A trial counts as a fall when the body fell, and as a get-up when it stands at the end and got up at least once.
# Prints one row per trial, writes <Out>\getup-stand.json. Exit code: 0 the share is met and every trial was a
# fall, 1 not, 2 a run produced no result.
param(
    [int]$Seed = 1,
    [int]$Floor = 26,
    [int]$Wall = 8,
    [int]$Table = 8,
    [int]$Chair = 8,
    [string]$Only,
    [double]$DurationS = 40.0,
    [double]$MinShare = 0.9,
    [int]$Parallel = 8,
    [string]$Tune,
    [string]$Film,
    [string]$Out
)
. "$PSScriptRoot\common.ps1"

if (-not $Out) { $Out = Join-Path $RepoRoot 'Saved\Matrix\getup_stand' }
$Out = [System.IO.Path]::GetFullPath($Out)
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
$scenarioDir = Join-Path $Out 'scenarios'
New-Item -ItemType Directory -Force $scenarioDir | Out-Null

$base = Read-JsonFile (Join-Path $RepoRoot 'Scenarios\getup_back.json')
$roomMap = '/Game/Generated/Maps/GetUpRoom'
$hitTime = 3.0
$dice = New-Object System.Random $Seed
function Roll([double]$from, [double]$to) { return $from + ($to - $from) * $dice.NextDouble() }
function Pick($items) { return $items[$dice.Next($items.Count)] }

# Where the body stands and which way it is knocked, by kind. The room is 600 by 500 cm about the origin; the
# table stands at (170, -120), 120 by 70 cm; the chair at (-170, 120), 42 by 42 cm, its back on the +y side.
function New-Place([string]$kind) {
    $reach = Roll 45 90
    switch ($kind) {
        'floor' { return @{ location = @(0, 0, 0); angle = (Roll 0 360); map = '' } }
        'wall' {
            switch ($dice.Next(4)) {
                0 { return @{ location = @((Roll -50 100), (250 - $reach), 0); angle = (Roll 40 140); map = $roomMap } }
                1 { return @{ location = @((-300 + $reach), (Roll -150 -50), 0); angle = (Roll 130 230); map = $roomMap } }
                2 { return @{ location = @((300 - $reach), (Roll 50 150), 0); angle = (Roll -50 50); map = $roomMap } }
                default { return @{ location = @((Roll -150 0), (-250 + $reach), 0); angle = (Roll 220 320); map = $roomMap } }
            }
        }
        'table' {
            if ($dice.Next(2) -eq 0) { return @{ location = @((110 - $reach), (Roll -140 -100), 0); angle = (Roll -40 40); map = $roomMap } }
            return @{ location = @((Roll 130 210), (-85 + $reach), 0); angle = (Roll 230 310); map = $roomMap }
        }
        default {
            if ($dice.Next(2) -eq 0) { return @{ location = @((-149 + $reach), (Roll 105 135), 0); angle = (Roll 140 220); map = $roomMap } }
            return @{ location = @((Roll -180 -160), (99 - $reach), 0); angle = (Roll 50 130); map = $roomMap }
        }
    }
}

$counts = [ordered]@{ floor = $Floor; wall = $Wall; table = $Table; chair = $Chair }
$kinds = @($counts.Keys)
if ($Only) { $kinds = @($Only.Split(',') | ForEach-Object { $_.Trim() }) }
$script:drawn = 0
function New-Trial([string]$kind, [int]$number) {
    $script:drawn++
    $place = New-Place $kind
    $struck = Pick @('spine_04', 'spine_04', 'neck_01')
    $impulse = [Math]::Round((Roll 200 320))
    $push = Pick @(0.0, 0.0, 0.1, 0.2)
    $angle = [Math]::Round(($place.angle + 360.0) % 360.0, 1)
    $radians = $angle * [Math]::PI / 180.0
    $name = ('getup_stand_{0:00}' -f $number)
    $body = $base.body | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $body.location_cm = @([Math]::Round($place.location[0], 1), [Math]::Round($place.location[1], 1), 0)
    $blow = [ordered]@{
        t          = $hitTime
        type       = 'impulse'
        body       = $struck
        impulse_ns = $impulse
        direction  = @([Math]::Round([Math]::Cos($radians), 6), [Math]::Round([Math]::Sin($radians), 6), 0)
    }
    if ($push -gt 0) { $blow.duration_s = $push }
    $scenario = [ordered]@{
        name       = $name
        duration_s = $DurationS
        body       = $body
        events     = @($blow)
        expect     = [ordered]@{ fell = $true; end_state = 'standing'; min_get_ups = 1 }
    }
    if ($place.map) { $scenario.map = $place.map }
    $file = Join-Path $scenarioDir "$name.json"
    Write-JsonFile $file $scenario
    return [ordered]@{ number = $number; name = $name; kind = $kind; body = $struck; angle = $angle; impulse = $impulse; push = $push
        location = $body.location_cm; file = $file; dir = (Join-Path $Out "runs\$name") }
}
$trials = @()
$number = 0
foreach ($kind in $kinds) {
    for ($index = 0; $index -lt $counts[$kind]; $index++) {
        $number++
        $trials += (New-Trial $kind $number)
    }
}

# A blow that the body rides out, or that a wall catches it from, is not a fall: such a trial is drawn again, up to
# five times, so that the stand has as many falls as it was asked for.
$started = Get-Date
$pending = @($trials)
for ($round = 0; $round -lt 6 -and $pending.Count -gt 0; $round++) {
    $jobs = @()
    foreach ($trial in $pending) {
        if (Test-Path $trial.dir) { Remove-Item $trial.dir -Recurse -Force }
        $job = @{ File = $trial.file; Out = $trial.dir }
        if ($Tune) { $job.Tune = $Tune }
        $jobs += $job
    }
    Invoke-ScenarioBatch -Jobs $jobs -Parallel $Parallel
    $again = @()
    for ($index = 0; $index -lt $trials.Count; $index++) {
        $trial = $trials[$index]
        if (-not ($pending | Where-Object { $_.number -eq $trial.number })) { continue }
        $run = Read-JsonFile (Join-Path $trial.dir 'metrics.json')
        if ($run -and $run.ok -and -not $run.metrics.fell -and $round -lt 5) {
            $trials[$index] = New-Trial $trial.kind $trial.number
            $again += $trials[$index]
        }
    }
    $pending = $again
}
$seconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 1)

$results = @()
$missing = @()
foreach ($trial in $trials) {
    $run = Read-JsonFile (Join-Path $trial.dir 'metrics.json')
    if (-not $run -or -not $run.ok) { $missing += $trial.name; continue }
    $ups = $run.control.get_up
    $stood = ($run.control.end_state -eq 'standing') -and ($ups.successes -ge 1)
    # When the body stood for good: the last time it went into Standing.
    $stoodAt = $null
    $log = Join-Path $trial.dir 'run.log'
    if ($stood -and (Test-Path $log)) {
        $line = Get-Content $log | Where-Object { $_ -match 'Body controller: standing at ([\d\.]+) s \(got up' } | Select-Object -Last 1
        if ($line -and $line -match 'standing at ([\d\.]+) s') { $stoodAt = [double]::Parse($Matches[1], [System.Globalization.CultureInfo]::InvariantCulture) }
    }
    $results += [ordered]@{
        number              = $trial.number
        name                = $trial.name
        kind                = $trial.kind
        body                = $trial.body
        angle               = $trial.angle
        impulse_ns          = $trial.impulse
        push_s              = $trial.push
        location_cm         = $trial.location
        fell                = [bool]$run.metrics.fell
        got_up              = $stood
        end_state           = $run.control.end_state
        attempts            = $ups.attempts
        successes           = $ups.successes
        stood_at_s          = $stoodAt
        given_up            = $ups.given_up
        assist_share_mean   = $ups.assist_force_share_mean
        assist_share_max    = $ups.assist_force_share_max
        joint_stretch_cm    = $run.metrics.max_joint_stretch_cm
        limit_violation_deg = $run.metrics.max_joint_limit_violation_deg
        limit_joint         = $run.metrics.worst_limit_joint
        joint_speed_rads    = $run.metrics.max_joint_speed_rads
        penetration_cm      = $run.penetration.max_world_cm
        hash                = $run.hashes.bodies
        file                = $trial.file
    }
}
if ($missing.Count -gt 0) {
    Write-Output "GETUP STAND ERROR: $($missing.Count) of $($trials.Count) run(s) produced no result, first: $($missing[0])"
    exit 2
}

$falls = @($results | Where-Object { $_.fell })
$up = @($falls | Where-Object { $_.got_up })
$share = 0.0
if ($falls.Count -gt 0) { $share = $up.Count / $falls.Count }
$verdict = 'OK'
if ($share -lt $MinShare -or $falls.Count -lt $results.Count) { $verdict = 'FAILED' }
$first = @($up | Where-Object { $_.attempts -eq 1 }).Count
Write-Output ("GETUP STAND {0}: {1} of {2} fall(s) end standing ({3:P0}, needed {4:P0}); {5} at the first attempt; {6} trial(s) without a fall; {9} blow(s) drawn; seed {7}, {8} s" -f `
    $verdict, $up.Count, $falls.Count, $share, $MinShare, $first, ($results.Count - $falls.Count), $Seed, $seconds, $script:drawn)
foreach ($kind in $kinds) {
    $of = @($results | Where-Object { $_.kind -eq $kind })
    if ($of.Count -gt 0) {
        Write-Output ('  {0,-6} {1} of {2} stand' -f $kind, @($of | Where-Object { $_.got_up }).Count, $of.Count)
    }
}
Write-Output ('  {0,3} {1,-6} {2,-9} {3,5} {4,4} {5,5} {6,-14} {7,8} {8,9} {9,7} {10,-22} {11}' -f `
    'n', 'kind', 'body', 'angle', 'N*s', 'push', 'at cm', 'attempts', 'stands at', 'assist', 'past limit deg', 'verdict')
foreach ($r in $results) {
    $verdictText = 'stands'
    if (-not $r.fell) { $verdictText = 'NO FALL' }
    elseif (-not $r.got_up) { $verdictText = "DOWN ($($r.end_state)): $($r.given_up)" }
    $stoodText = '-'
    if ($null -ne $r.stood_at_s) { $stoodText = ('{0:N1}' -f [double]$r.stood_at_s) }
    Write-Output ('  {0,3} {1,-6} {2,-9} {3,5:N0} {4,4:N0} {5,5:N1} {6,-14} {7,8} {8,9} {9,7:N2} {10,-22} {11}' -f `
        $r.number, $r.kind, $r.body, $r.angle, $r.impulse_ns, $r.push_s, ('{0:N0},{1:N0}' -f [double]$r.location_cm[0], [double]$r.location_cm[1]),
        $r.attempts, $stoodText, [double]$r.assist_share_mean, ('{0:N1} {1}' -f [double]$r.limit_violation_deg, $r.limit_joint), $verdictText)
}

Write-JsonFile (Join-Path $Out 'getup-stand.json') ([ordered]@{
    ok       = $true
    pass     = ($verdict -eq 'OK')
    seed     = $Seed
    seconds  = $seconds
    falls    = $falls.Count
    stand    = $up.Count
    share    = $share
    needed   = $MinShare
    tune     = $Tune
    results  = $results
})

if ($Film) {
    $toFilm = $results
    if ($Film -ne 'all') { $toFilm = @($results | Where-Object { $_.fell -and -not $_.got_up }) }
    foreach ($r in $toFilm) {
        $x = [double]$r.location_cm[0]
        $y = [double]$r.location_cm[1]
        $camera = ('{0},{1},170;{2},{3},50' -f ($x + 260), ($y - 260), $x, $y)
        $filmArguments = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'film.ps1'), '-File', $r.file, '-Tag', $r.name,
            '-Times', '3.2,4,5,6,7,8,9,10,11,12,13,14,16,18,20,24', '-Camera', $camera, '-Cols', 8)
        if ($Tune) { $filmArguments += @('-Tune', $Tune) }
        & powershell @filmArguments | Select-Object -Last 1
    }
}
if ($verdict -eq 'OK') { exit 0 }
exit 1
