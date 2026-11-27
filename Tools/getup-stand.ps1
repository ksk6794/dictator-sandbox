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
#   ... -HoldHands 1            every fallen body is held by one hand (2: by two) that takes hold of it where it lies
#                               (-HoldBone spine_04), from -HoldFromS 5.5 for -HoldForS 15 seconds, with the grab
#                               switched on for the trial. So that the hands come down on a body at rest, a held
#                               body begins to get up after 4 s of lying, not 1.5 (getup.rest_s, for these trials
#                               only). The stand then also counts the bodies the hands took hold of, on that
#                               part itself or on whatever lay nearest, the grips that came off by themselves,
#                               and the bodies that pulled free and stood before the hold's time was up (a body
#                               cannot stand with a grip on it that keeps its place near the floor). Not
#                               together with -PressN
#   ... -PropsFile <file.json>  a list of things with mass (a scenario's "props") put into every trial on the bare
#                               floor: the stand among movable furniture (with -Only floor -Floor 50, and
#                               -Tune "reactions.thing_support=1" for a body that can stand on things)
#   ... -Tune "name=value,..."  tuning overrides for every run
#   ... -TuningDir <dir>        the tuning files from this directory instead of Config\Tuning (for values -Tune cannot give,
#                               such as a clip's path)
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
    [string]$PropsFile,
    [string]$FurnitureRoom = 'getup_room',
    [int]$HoldHands = 0,
    [string]$HoldBone = 'spine_04',
    [double]$HoldFromS = 5.5,
    [double]$HoldForS = 15.0,
    [string]$Tune,
    [string]$TuningDir,
    [string]$Film,
    [string]$Out
)
. "$PSScriptRoot\common.ps1"
$movables = @()
if ($PropsFile) {
    $readProps = $null
    if (Test-Path $PropsFile) { $readProps = Read-JsonFile $PropsFile }
    if ($null -ne $readProps) { $movables = @($readProps) }
    $unnamed = @($movables | Where-Object { $null -eq $_ -or -not $_.name -or -not $_.size_cm -or -not $_.location_cm })
    if ($movables.Count -eq 0 -or $unnamed.Count -gt 0) {
        Write-Output "GETUP STAND ERROR: '$PropsFile' is not a list of props (each with name, size_cm, mass_kg, location_cm), or cannot be read"
        exit 2
    }
}
if ($HoldHands -lt 0 -or $HoldHands -gt 2) { Write-Output 'GETUP STAND ERROR: -HoldHands is 0, 1 or 2'; exit 2 }
if ($HoldHands -gt 0 -and $PressN -gt 0) { Write-Output 'GETUP STAND ERROR: -HoldHands and -PressN each count the bodies that stood in their own time; give one of them'; exit 2 }
if ($HoldHands -gt 0 -and $HoldForS -lt 2.0) { Write-Output 'GETUP STAND ERROR: -HoldForS has to be 2 s or more (the hands take 1.4 s to come down and close)'; exit 2 }

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
$roomSpec = Read-JsonFile (Join-Path $RepoRoot "Specs\rooms\$FurnitureRoom.json")
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
    Write-Output "GETUP STAND ERROR: Specs\rooms\$FurnitureRoom.json has no furniture named 'table' and 'chair'"
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
    $struck = Pick @('spine_04', 'spine_04', 'head')
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
    if ($HoldHands -gt 0) {
        # The grab is on for the trial, the body lies longer before it begins to get up, and one or two hands
        # come down on it where it lies: fast through the air to above the part, slowly onto it, then the grip.
        $switches = @(
            [ordered]@{ t = 0.0; type = 'console'; command = 'Sandbox.Tune effectors.hand.grab=1' },
            [ordered]@{ t = 0.0; type = 'console'; command = 'Sandbox.Tune getup.rest_s=4' })
        $happenings = $switches + $happenings
        $handNames = @('hand_left', 'hand_right')
        $sides = @(0.0)
        if ($HoldHands -ge 2) { $sides = @(-10.0, 10.0) }
        for ($h = 0; $h -lt $sides.Count; $h++) {
            $onIt = @($sides[$h], 0, 12)
            $happenings += [ordered]@{ t = $HoldFromS; type = 'effector'; effector = $handNames[$h]; at_bone = $HoldBone; path = @(
                [ordered]@{ t = 0.0; location_cm = @(($body.location_cm[0] + $sides[$h]), $body.location_cm[1], 260); grip = 0 },
                [ordered]@{ t = 0.4; location_cm = @($sides[$h], 0, 80); grip = 0 },
                [ordered]@{ t = 1.0; location_cm = $onIt; grip = 0 },
                [ordered]@{ t = 1.3; location_cm = $onIt; grip = 0 },
                [ordered]@{ t = 1.4; location_cm = $onIt; grip = 1 },
                [ordered]@{ t = $HoldForS; location_cm = $onIt; grip = 1 },
                [ordered]@{ t = ($HoldForS + 0.1); location_cm = $onIt; grip = 0 },
                [ordered]@{ t = ($HoldForS + 0.6); location_cm = @($sides[$h], 0, 120); grip = 0 }) }
        }
    }
    $scenario = [ordered]@{
        name       = $name
        duration_s = $DurationS
        body       = $body
        events     = $happenings
        expect     = [ordered]@{ fell = $true; end_state = 'standing'; min_get_ups = 1 }
    }
    if ($place.map) { $scenario.map = $place.map }
    elseif ($movables.Count -gt 0) { $scenario.props = $movables }
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
        if ($TuningDir) { $job.TuningDir = $TuningDir }
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
    $toStandS = $null
    $log = Join-Path $trial.dir 'run.log'
    if ($stood -and (Test-Path $log)) {
        $stoodLines = @(Get-Content $log | Where-Object { $_ -match 'Body controller: standing at ([\d\.]+) s \(got up' })
        $line = $stoodLines | Select-Object -Last 1
        if ($line -and $line -match 'standing at ([\d\.]+) s') { $stoodAt = [double]::Parse($Matches[1], [System.Globalization.CultureInfo]::InvariantCulture) }
        # How long from the first attempt to the first standing (decision 13: how much longer a held body takes to stand).
        $firstLine = $stoodLines | Select-Object -First 1
        if ($firstLine -and $firstLine -match 'standing at ([\d\.]+) s' -and $null -ne $ups.first_started_s -and [double]$ups.first_started_s -ge 0) {
            $toStandS = [Math]::Round([double]::Parse($Matches[1], [System.Globalization.CultureInfo]::InvariantCulture) - [double]$ups.first_started_s, 3)
        }
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
    # What the holding hands did, if there are any.
    $tookHold = 0
    $onThePart = 0
    $slipped = 0
    if ($HoldHands -gt 0) {
        # A hand that could not be sent to the part of the body went to a place of the world instead.
        if (@($run.checks | Where-Object { $_.name -eq 'all_events_applied' -and -not $_.pass }).Count -gt 0) {
            Write-Output "GETUP STAND ERROR: in trial $($trial.number) a hand could not be sent to '$HoldBone' (is it a body of the character?). Log: $(Join-Path $trial.dir 'run.log')"
            exit 2
        }
        if ($run.effectors -and $run.effectors.list) {
            foreach ($handName in @('hand_left', 'hand_right')) {
                $oneHand = $run.effectors.list.$handName
                if ($oneHand -and [int]$oneHand.grabs -ge 1) {
                    $tookHold++
                    if ([string]$oneHand.last_grabbed -eq $HoldBone) { $onThePart++ }
                    $slipped += [int]$oneHand.grab_slips + [int]$oneHand.grab_overloads
                }
            }
        }
    }
    # What the world gave the body while it got up, by direction and by body (control.authority, decision 15).
    $authority = $null
    $authorityBodies = @()
    if ($run.control.authority -and $run.control.authority.states -and $run.control.authority.states.get_up) {
        $authority = $run.control.authority.states.get_up
        foreach ($given in @($run.control.authority.get_up_bodies)) {
            if ($given) { $authorityBodies += [ordered]@{ body = $given.body; source = $given.source; mean = $given.mean; up = $given.up_mean; horizontal = $given.horizontal_mean } }
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
        hands_took_hold     = $tookHold
        hands_on_the_part   = $onThePart
        hands_slipped       = $slipped
        got_up              = $stood
        end_state           = $run.control.end_state
        attempts            = $ups.attempts
        successes           = $ups.successes
        stood_at_s          = $stoodAt
        to_stand_s          = $toStandS
        # R2's gate (decisions 16 and 21): the last attempt's clip and its shape at 70 cm of pelvis (C27), the longest pose held
        # while getting up (C19).
        clip                = $(if ($ups.clip) { ([string]$ups.clip).Split('.')[-1] } else { $null })
        rise_trunk_deg      = $ups.rise_trunk_deg
        rise_feet_cm        = $ups.rise_feet_cm
        held_pose_s         = $(if ($run.metrics.stillness -and $run.metrics.stillness.target_still_longest_by_state_s) { $run.metrics.stillness.target_still_longest_by_state_s.get_up } else { $null })
        given_up            = $ups.given_up
        assist_share_mean   = $ups.assist_force_share_mean
        assist_share_max    = $ups.assist_force_share_max
        led_attempts        = $ups.led_attempts
        not_led_attempts    = $ups.not_led_attempts
        authority_up        = $(if ($authority) { $authority.up_mean } else { $null })
        authority_horizontal = $(if ($authority) { $authority.horizontal_mean } else { $null })
        authority_down      = $(if ($authority) { $authority.down_mean } else { $null })
        authority_peak      = $(if ($authority) { $authority.peak } else { $null })
        authority_bodies    = $authorityBodies
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
if ($HoldHands -gt 0) {
    # Of the bodies every hand took hold of: how many stood for the first time while they were held, and how
    # many grips came off by themselves (slipped or torn open).
    $heldUntil = $HoldFromS + $HoldForS
    $held = @($results | Where-Object { $_.hands_took_hold -ge $HoldHands })
    $under = 0
    foreach ($r in $held) {
        $log = Join-Path $Out "runs\$($r.name)\run.log"
        $firstStood = $null
        if (Test-Path $log) {
            $line = Get-Content $log | Where-Object { $_ -match 'Body controller: standing at ([\d\.]+) s \(got up' } | Select-Object -First 1
            if ($line -and $line -match 'standing at ([\d\.]+) s') { $firstStood = [double]::Parse($Matches[1], [System.Globalization.CultureInfo]::InvariantCulture) }
        }
        if ($null -ne $firstStood -and $firstStood -lt $heldUntil) { $under++ }
    }
    $cameOff = @($held | Where-Object { $_.hands_slipped -gt 0 }).Count
    $onPart = @($held | Where-Object { $_.hands_on_the_part -ge $HoldHands }).Count
    Write-Output ('  held by {0} hand(s) sent to {1}, grips closed from {2:N1} to {3:N1} s: the hands took hold of {4} of {5} bodies ({6} by {1} itself); a grip came off by itself on {7} of those, {8} pulled free and stood before the time was up, {9} stand in the end' -f `
        $HoldHands, $HoldBone, ($HoldFromS + 1.4), $heldUntil, $held.Count, $results.Count, $onPart, $cameOff, $under, @($held | Where-Object { $_.got_up }).Count)
}
# From the first attempt to the first standing, the median (the run's log and control.get_up.first_started_s).
$spans = @($results | Where-Object { $null -ne $_.to_stand_s } | ForEach-Object { [double]$_.to_stand_s } | Sort-Object)
if ($spans.Count -gt 0) {
    Write-Output ('  from the first attempt to the first standing: median {0:N2} s of {1} bodies (10th percentile {2:N2}, 90th {3:N2})' -f `
        $spans[[int][Math]::Floor(($spans.Count - 1) / 2)], $spans.Count, $spans[[int][Math]::Floor(($spans.Count - 1) / 10)], $spans[[int][Math]::Floor(9 * ($spans.Count - 1) / 10)])
}
# What the world gave while the bodies got up (control.authority): the mean of the falls' means by direction, and the bodies given
# most, each by the mean of its share over the falls that got up at all.
$withAuthority = @($falls | Where-Object { $null -ne $_.authority_up })
if ($withAuthority.Count -gt 0) {
    $meanOf = { param($name) ($withAuthority | ForEach-Object { [double]$_.$name } | Measure-Object -Average).Average }
    $byBody = @{}
    foreach ($r in $withAuthority) {
        foreach ($given in @($r.authority_bodies)) {
            if (-not $given) { continue }
            $key = "$($given.body) ($($given.source))"
            if (-not $byBody.ContainsKey($key)) { $byBody[$key] = 0.0 }
            $byBody[$key] += [double]$given.mean
        }
    }
    $most = @($byBody.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 5 | ForEach-Object { '{0} {1:N2}' -f $_.Key, ($_.Value / $withAuthority.Count) })
    Write-Output ('  authority while getting up, of body weight: mean {0:N2}, up {1:N2}, over the floor {2:N2}, down {3:N2}; peak {4:N2} in the median fall; given most: {5}' -f `
        (& $meanOf 'assist_share_mean'), (& $meanOf 'authority_up'), (& $meanOf 'authority_horizontal'), (& $meanOf 'authority_down'),
        (@($withAuthority | ForEach-Object { [double]$_.authority_peak } | Sort-Object)[[int][Math]::Floor(($withAuthority.Count - 1) / 2)]), ($most -join ', '))
}
$notLed = @($results | Where-Object { [int]$_.not_led_attempts -gt 0 })
if ($notLed.Count -gt 0) {
    Write-Output ('  GETUP STAND WARNING: {0} trial(s) asked for the led get-up and were not led (decision 17), first: trial {1}' -f $notLed.Count, $notLed[0].number)
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
    tuning_dir = $TuningDir
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
        if ($TuningDir) { $filmArguments += @('-TuningDir', $TuningDir) }
        & powershell @filmArguments | Select-Object -Last 1
    }
}
if ($verdict -eq 'OK') { exit 0 }
exit 1
