# The stand of M3 part 6: knocks the NPC down many times, each time differently, and counts how often it is back
# on its feet. The criterion of the part: it gets up in 90% of 50 falls (docs/PRD.md).
#   powershell -ExecutionPolicy Bypass -File Tools\getup-stand.ps1
#   ... -Seed 1                 the trials are drawn from this seed: the same seed, the same 50 falls
#   ... -Floor 26 -Wall 8 -Table 8 -Chair 8     how many falls of each kind
#   ... -Only "wall,chair"      only these kinds
#   ... -DurationS 40           simulated seconds of each trial at most (the blow comes at 3 s)
#   ... -StopStandingS 3        a trial ends once its body has stood this long with nothing left to happen to it
#                               (most stand 8 s after the blow); 0 runs every trial for the whole -DurationS
#   ... -MinShare 0.9           the share of falls that must end standing
#   ... -PressN 300             every fallen body is pressed down on the pelvis with this force (-PressFromS 5 to
#                               -PressForS 20 seconds later): what a hand does to a get-up, on 50 falls instead of
#                               one. The stand then also counts the bodies that stood while they were pressed
#   ... -Tune "name=value,..."  tuning overrides for every run
#   ... -Film failed            also a sheet of pictures of every trial that did not end standing ("all": of every
#                               trial; "35,43": of these trials; Saved\Films\getup_stand_<n>)
#   ... -Out <dir>              output directory (default Saved\Matrix\getup_stand)
#
# The kinds. "floor": on the bare floor, a blow or a shove of 200 to 320 N*s on the chest or the head from any
# direction. "wall", "table", "chair": in the room of Specs\rooms\getup_room.json, the body standing 45 to 90 cm
# from a wall, the table or the chair (65 to 110 cm when that is at its side) and knocked toward it, within 50
# degrees. The furniture is static: a table in the middle of the room and a chair drawn up to its +y side. Where
# the furniture stands is read from the spec; a body is never put where its arms, out as it appears, would touch
# a piece or a wall, and the falls at a wall are in the corners' quarters of the room, away from the furniture:
# how near each of them came to it is measured and reported.
# A direction is the way the blow moves the body, as in Tools\push-matrix.ps1: 0 toward the character's left
# (+x), 90 forward (+y), 180 to its right, 270 backward.
#
# A trial counts as a fall when the body fell, and as a get-up when it stands at the end and got up at least once.
# Prints one row per trial, writes <Out>\getup-stand.json. Exit code: 0 the share is met and every trial was a
# fall, 1 not, 2 a run produced no result or a trial was drawn inside the furniture.
param(
    [int]$Seed = 1,
    [int]$Floor = 26,
    [int]$Wall = 8,
    [int]$Table = 8,
    [int]$Chair = 8,
    [string]$Only,
    [double]$DurationS = 40.0,
    [double]$StopStandingS = 3.0,
    [double]$MinShare = 0.9,
    [int]$Parallel = 8,
    [double]$PressN = 0,
    [double]$PressFromS = 5.0,
    [double]$PressForS = 20.0,
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
$hitTime = 3.0
$dice = New-Object System.Random $Seed
function Roll([double]$from, [double]$to) { return $from + ($to - $from) * $dice.NextDouble() }
function Pick($items) { return $items[$dice.Next($items.Count)] }

# The room and its furniture, from the spec. A piece's footprint is the rectangle of floor its boxes stand over.
$roomSpec = Read-JsonFile (Join-Path $RepoRoot 'Specs\rooms\getup_room.json')
$roomMap = $roomSpec.output
$halfX = [double]$roomSpec.size_cm[0] / 2.0
$halfY = [double]$roomSpec.size_cm[1] / 2.0
$footprints = @{}
foreach ($piece in @($roomSpec.furniture)) {
    $turn = [double]$piece.yaw_deg * [Math]::PI / 180.0
    $xs = @()
    $ys = @()
    $pieceBoxes = @()
    foreach ($box in $piece.boxes) {
        $pieceBoxes += @{ x = [double]$box.center_cm[0]; y = [double]$box.center_cm[1]; z = [double]$piece.location_cm[2] + [double]$box.center_cm[2]
            halfX = [double]$box.size_cm[0] / 2.0; halfY = [double]$box.size_cm[1] / 2.0; halfZ = [double]$box.size_cm[2] / 2.0 }
        foreach ($sideX in @(-0.5, 0.5)) {
            foreach ($sideY in @(-0.5, 0.5)) {
                $localX = [double]$box.center_cm[0] + $sideX * [double]$box.size_cm[0]
                $localY = [double]$box.center_cm[1] + $sideY * [double]$box.size_cm[1]
                $xs += [double]$piece.location_cm[0] + [Math]::Cos($turn) * $localX - [Math]::Sin($turn) * $localY
                $ys += [double]$piece.location_cm[1] + [Math]::Sin($turn) * $localX + [Math]::Cos($turn) * $localY
            }
        }
    }
    $spanX = $xs | Measure-Object -Minimum -Maximum
    $spanY = $ys | Measure-Object -Minimum -Maximum
    $footprints[$piece.name] = @{ x0 = $spanX.Minimum; x1 = $spanX.Maximum; y0 = $spanY.Minimum; y1 = $spanY.Maximum
        atX = [double]$piece.location_cm[0]; atY = [double]$piece.location_cm[1]; cos = [Math]::Cos($turn); sin = [Math]::Sin($turn); boxes = $pieceBoxes }
}
$tableAt = $footprints['table']
$chairAt = $footprints['chair']
if (-not $tableAt -or -not $chairAt) {
    Write-Output 'GETUP STAND ERROR: Specs\rooms\getup_room.json has no furniture named "table" and "chair"'
    exit 2
}
# How far a point is from the nearest box of the furniture, cm. Further than 60 cm over the floor from a piece
# it is that distance over the floor (the boxes are no nearer); nearer, it is measured to the boxes themselves,
# so an arm above the table top or a foot between its legs counts as what it is.
function Get-FurnitureDistance([double]$x, [double]$y, [double]$z) {
    $nearest = [double]::MaxValue
    foreach ($at in $footprints.Values) {
        $offX = [Math]::Max([Math]::Max($at.x0 - $x, $x - $at.x1), 0.0)
        $offY = [Math]::Max([Math]::Max($at.y0 - $y, $y - $at.y1), 0.0)
        $overFloor = [Math]::Sqrt($offX * $offX + $offY * $offY)
        if ($overFloor -gt 60.0) {
            $nearest = [Math]::Min($nearest, $overFloor)
            continue
        }
        foreach ($box in $at.boxes) {
            $inX = [Math]::Max([Math]::Abs($at.cos * ($x - $at.atX) + $at.sin * ($y - $at.atY) - $box.x) - $box.halfX, 0.0)
            $inY = [Math]::Max([Math]::Abs(-$at.sin * ($x - $at.atX) + $at.cos * ($y - $at.atY) - $box.y) - $box.halfY, 0.0)
            $inZ = [Math]::Max([Math]::Abs($z - $box.z) - $box.halfZ, 0.0)
            $nearest = [Math]::Min($nearest, [Math]::Sqrt($inX * $inX + $inY * $inY + $inZ * $inZ))
        }
    }
    return $nearest
}

# Where the body stands and which way it is knocked, by kind. The room is 600 by 500 cm about the origin. The
# table (120 by 70 cm) stands in its middle and the chair (42 by 42 cm) at the table's +y side, its back away from
# the table: so the table is met from its open long side (a fall forward) and from an end (to the left), the
# chair from its side (to the right) and from behind (backward), and the walls in the room's four corner quarters,
# 1.4 m and more from the furniture. The body faces +y.
#
# The body appears in its reference pose, the arms out (fingertips 57 cm to either side and 26 cm ahead, at the
# height of a chair's back), and brings them down within half a second. That box, with 5 cm to spare, has to be
# clear of the furniture and the walls. So a thing at the body's side is 20 cm further off than one before or
# behind it: 65 to 110 cm instead of 45 to 90.
$spawnBox = @{ side = 58.0; ahead = 28.0; behind = 12.0 }
$spareCm = 5.0
$besideCm = 20.0
# How much room the box of a body appearing at a point has to the furniture and to the walls, cm (0: it touches).
function Get-SpawnRoom([double]$x, [double]$y) {
    $toFurniture = [double]::MaxValue
    foreach ($at in $footprints.Values) {
        $offX = [Math]::Max([Math]::Max($at.x0 - ($x + $spawnBox.side), ($x - $spawnBox.side) - $at.x1), 0.0)
        $offY = [Math]::Max([Math]::Max($at.y0 - ($y + $spawnBox.ahead), ($y - $spawnBox.behind) - $at.y1), 0.0)
        $toFurniture = [Math]::Min($toFurniture, [Math]::Sqrt($offX * $offX + $offY * $offY))
    }
    $toWall = [Math]::Min([Math]::Min($halfX - ($x + $spawnBox.side), $halfX + ($x - $spawnBox.side)),
        [Math]::Min($halfY - ($y + $spawnBox.ahead), $halfY + ($y - $spawnBox.behind)))
    return @{ furniture = $toFurniture; wall = $toWall }
}
function New-Place([string]$kind) {
    $reach = Roll 45 90
    $beside = $reach + $besideCm
    switch ($kind) {
        'floor' { return @{ location = @(0, 0, 0); angle = (Roll 0 360); map = '' } }
        'wall' {
            switch ($dice.Next(4)) {
                0 { return @{ location = @((Roll 140 220), ($halfY - $reach), 0); angle = (Roll 40 140); map = $roomMap } }
                1 { return @{ location = @((-$halfX + $beside), (Roll 110 170), 0); angle = (Roll 130 230); map = $roomMap } }
                2 { return @{ location = @(($halfX - $beside), (Roll -170 -110), 0); angle = (Roll -50 50); map = $roomMap } }
                default { return @{ location = @((Roll -220 -140), (-$halfY + $reach), 0); angle = (Roll 220 320); map = $roomMap } }
            }
        }
        'table' {
            $middleX = ($tableAt.x0 + $tableAt.x1) / 2.0
            if ($dice.Next(2) -eq 0) { return @{ location = @(($tableAt.x0 - $beside), (Roll ($tableAt.y0 + 10) ($tableAt.y1 - 10)), 0); angle = (Roll -40 40); map = $roomMap } }
            return @{ location = @((Roll ($middleX - 40) ($middleX + 40)), ($tableAt.y0 - $reach), 0); angle = (Roll 50 130); map = $roomMap }
        }
        default {
            $middleX = ($chairAt.x0 + $chairAt.x1) / 2.0
            $middleY = ($chairAt.y0 + $chairAt.y1) / 2.0
            if ($dice.Next(2) -eq 0) { return @{ location = @(($chairAt.x1 + $beside), (Roll ($middleY + 5) ($middleY + 25)), 0); angle = (Roll 140 220); map = $roomMap } }
            return @{ location = @((Roll ($middleX - 10) ($middleX + 10)), ($chairAt.y1 + $reach), 0); angle = (Roll 230 310); map = $roomMap }
        }
    }
}

$counts = [ordered]@{ floor = $Floor; wall = $Wall; table = $Table; chair = $Chair }
$kinds = @($counts.Keys)
if ($Only) { $kinds = @($Only.Split(',') | ForEach-Object { $_.Trim() }) }
$script:drawn = 0
$script:misplaced = @()
function New-Trial([string]$kind, [int]$number) {
    $script:drawn++
    $place = New-Place $kind
    if ($place.map) {
        $room = Get-SpawnRoom $place.location[0] $place.location[1]
        if ($room.furniture -lt $spareCm -or $room.wall -lt $spareCm) {
            $script:misplaced += ('trial {0} ({1}) at {2:N0}, {3:N0}: its arms out, {4:N0} cm from the furniture and {5:N0} cm from a wall' -f `
                $number, $kind, $place.location[0], $place.location[1], $room.furniture, $room.wall)
        }
    }
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
    $happenings = @($blow)
    if ($PressN -gt 0) {
        $happenings += [ordered]@{ t = $PressFromS; type = 'impulse'; body = 'pelvis'; impulse_ns = [Math]::Round($PressN * $PressForS, 3)
            direction = @(0, 0, -1); duration_s = $PressForS }
    }
    $scenario = [ordered]@{
        name       = $name
        duration_s = $DurationS
        body       = $body
        events     = $happenings
        expect     = [ordered]@{ fell = $true; end_state = 'standing'; min_get_ups = 1 }
    }
    if ($place.map) { $scenario.map = $place.map }
    if ($StopStandingS -gt 0) { $scenario.end_when_standing_s = $StopStandingS }
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
    if ($script:misplaced.Count -gt 0) {
        Write-Output "GETUP STAND ERROR: a body would appear with less than $spareCm cm to the furniture or a wall: $($script:misplaced[0])"
        exit 2
    }
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
    # A fall at a wall is to be a fall at a wall only: the nearest any of its bodies came to the furniture, over
    # the run's checkpoints (every 0.5 s; a body's point is its bone, so a hand or the head reaches 25 cm further).
    # A body that needs several attempts can cross the room before it stands, so the whole run is looked at.
    $nearFurniture = $null
    if ($trial.kind -eq 'wall') {
        $trace = Read-JsonFile (Join-Path $trial.dir 'trace.json')
        if ($trace) {
            $nearFurniture = [double]::MaxValue
            foreach ($checkpoint in $trace.checkpoints) {
                foreach ($traced in $checkpoint.bodies) {
                    $nearFurniture = [Math]::Min($nearFurniture, (Get-FurnitureDistance ([double]$traced.p[0]) ([double]$traced.p[1]) ([double]$traced.p[2])))
                }
            }
            $nearFurniture = [Math]::Round($nearFurniture, 1)
        }
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
        furniture_clearance_cm = $nearFurniture
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
if ($PressN -gt 0) {
    # The first time each body stood, from its log: a body that stands while it is pressed has not been kept down.
    $pressedUntil = $PressFromS + $PressForS
    $under = 0
    foreach ($r in $results) {
        $log = Join-Path $Out "runs\$($r.name)\run.log"
        $firstStood = $null
        if (Test-Path $log) {
            $line = Get-Content $log | Where-Object { $_ -match 'Body controller: standing at ([\d\.]+) s \(got up' } | Select-Object -First 1
            if ($line -and $line -match 'standing at ([\d\.]+) s') { $firstStood = [double]::Parse($Matches[1], [System.Globalization.CultureInfo]::InvariantCulture) }
        }
        if ($null -ne $firstStood -and $firstStood -lt $pressedUntil) { $under++ }
    }
    Write-Output ('  pressed with {0:N0} N from {1:N0} to {2:N0} s: {3} of {4} bodies stood while they were pressed' -f $PressN, $PressFromS, $pressedUntil, $under, $results.Count)
}
$atWall = @($results | Where-Object { $null -ne $_.furniture_clearance_cm } | Sort-Object { [double]$_.furniture_clearance_cm })
if ($atWall.Count -gt 0) {
    Write-Output ('  falls at a wall keep {0:N0} cm and more from the table and the chair (the nearest: trial {1})' -f [double]$atWall[0].furniture_clearance_cm, $atWall[0].number)
    if ([double]$atWall[0].furniture_clearance_cm -lt 30.0) {
        Write-Output ('  GETUP STAND WARNING: trial {0}, a fall at a wall, came within a hand''s reach (30 cm) of the furniture' -f $atWall[0].number)
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
    if ($Film -match '^[\d,\s]+$') {
        $numbers = @($Film.Split(',') | ForEach-Object { [int]$_.Trim() })
        $toFilm = @($results | Where-Object { $numbers -contains $_.number })
    }
    elseif ($Film -ne 'all') { $toFilm = @($results | Where-Object { $_.fell -and -not $_.got_up }) }
    foreach ($r in $toFilm) {
        $x = [double]$r.location_cm[0]
        $y = [double]$r.location_cm[1]
        $cameraX = $x + 260
        $cameraY = $y - 260
        if ($r.kind -ne 'floor') {
            # In the room the camera has to stand inside the walls, or the pictures are black.
            $cameraX = [Math]::Max(-$halfX + 25, [Math]::Min($halfX - 25, $cameraX))
            $cameraY = [Math]::Max(-$halfY + 25, [Math]::Min($halfY - 25, $cameraY))
        }
        $camera = ('{0},{1},170;{2},{3},50' -f $cameraX, $cameraY, $x, $y)
        $filmArguments = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'film.ps1'), '-File', $r.file, '-Tag', $r.name,
            '-Times', '3.2,4,5,6,7,8,9,10,11,12,13,14,16,18,20,24', '-Camera', $camera, '-Cols', 8)
        if ($Tune) { $filmArguments += @('-Tune', $Tune) }
        & powershell @filmArguments | Select-Object -Last 1
    }
}
if ($verdict -eq 'OK') { exit 0 }
exit 1
