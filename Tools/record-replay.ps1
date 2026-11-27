# Records the hands of a session and replays the recording as a scenario, and holds the replay against what
# the session saw: the same states of the body in the same frames, the pelvis in the same place, the hands
# having touched the same bodies with the same impulse.
#   powershell -ExecutionPolicy Bypass -File Tools\record-replay.ps1                 the hands' own session (play.ps1 -Demo), then its replay
#   ... -Recorded <name>            do not record: replay Saved\Recordings\<name>, made by play.ps1 -Record <name>
#   ... -Name <scenario>            the scene of the session (default room_stand_npc)
#   ... -PelvisCm 1 -ImpulsePct 5 -Frames 1   how far the replay may be from the session: the pelvis's place,
#                                   each hand's contact impulse, the frame of each change of state
#   ... -Twice                      replay a second time and require the two replays to have the same hashes
#   ... -Perception                 the session sees (play.ps1 -Demo -Perception, recorded as demo_seeing): the body
#                                   is told what moves, with gaze, anticipation and the guard on, and so is its replay
#   ... -Takes                      the session of three takes (play.ps1 -Demo -Takes): the character is put back
#                                   before each, each take is a recording of its own, and each is replayed and held
#                                   against what the session saw of that take
#   ... -Walk                       the way a person in the headset takes, with a made-up headset (play.ps1 -Demo -Walk,
#                                   recorded as demo_walk): the stick walks the player's body into the character, along
#                                   it, turns and walks away; the hands and the body are replayed and held against it
# A session and a scenario are not the same run: the session's hands exist from its first frame, and a scenario's
# hashes are of every frame of its own. So the replay is held to the session by figures, and to itself by hashes.
# The verdict goes to Saved\Tools\record-replay.json as well (-Takes: record-replay-takes.json, -Walk: record-replay-walk.json,
# -Perception: record-replay-seeing.json, -Recorded <name>: record-replay-<name>.json); Tools\bootstrap.ps1 decides by it.
# Exit code: 0 the replay ends as the session did, 1 it does not, 2 something could not run.
param(
    [string]$Name = 'room_stand_npc',
    [string]$Recorded,
    [double]$PelvisCm = 1.0,
    [double]$ImpulsePct = 5.0,
    [int]$Frames = 1,
    [switch]$Twice,
    [switch]$Takes,
    [switch]$Walk,
    [switch]$Perception
)
. "$PSScriptRoot\common.ps1"

$resultName = 'record-replay'
if ($Takes) { $resultName = 'record-replay-takes' } elseif ($Recorded) { $resultName = "record-replay-$Recorded" } elseif ($Walk) { $resultName = 'record-replay-walk' } elseif ($Perception) { $resultName = 'record-replay-seeing' }
$verdictFile = Join-Path $ToolsOut "$resultName.json"
if (Test-Path $verdictFile) { Remove-Item $verdictFile -Force }
function Stop-Replay([int]$Code, [string]$Line, [string[]]$More = @()) {
    Write-JsonFile $verdictFile ([ordered]@{ ok = ($Code -eq 0); exit_code = $Code; verdict = $Line })
    Write-Output $Line
    $More | ForEach-Object { Write-Output $_ }
    exit $Code
}

if ($Takes) {
    $demo = & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'play.ps1') -Name $Name -Demo -Takes
    if ($LASTEXITCODE -ne 0) {
        Stop-Replay 2 "RECORD REPLAY ERROR: the session of takes did not give its recordings" @($demo | ForEach-Object { "  $_" })
    }
    $worst = 0
    $report = @()
    $good = 0
    $takeNames = @('demo_takes', 'demo_takes_2', 'demo_takes_3')
    foreach ($take in $takeNames) {
        $more = @()
        if ($Twice) { $more += '-Twice' }
        $lines = @(& powershell -ExecutionPolicy Bypass -File $PSCommandPath -Name $Name -Recorded $take -PelvisCm $PelvisCm -ImpulsePct $ImpulsePct -Frames $Frames @more)
        $code = $LASTEXITCODE
        if ($code -eq 0) { $good++ }
        if ($code -gt $worst) { $worst = $code }
        $report += "  $($lines[0])"
        $report += @($lines | Select-Object -Skip 1 | Where-Object { $_ -match 'FAIL|pelvis|impulse \(' -and $_ -notmatch '0\.00 N\*s\s+replay 0\.00' } | ForEach-Object { "  $_" })
    }
    $verdict = 'OK'
    if ($worst -ne 0) { $verdict = 'FAILED' }
    Stop-Replay $worst "RECORD REPLAY TAKES ${verdict}: $good of $($takeNames.Count) takes replay to what the session saw of them" $report
}

$recordName = $Recorded
if (-not $recordName) {
    $recordName = 'demo_hands'
    $seeing = @()
    if ($Perception) { $recordName = 'demo_seeing'; $seeing = @('-Perception') }
    if ($Walk) { $recordName = 'demo_walk'; $seeing += '-Walk' }
    $demo = & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'play.ps1') -Name $Name -Demo -Record $recordName @seeing
    if ($LASTEXITCODE -ne 0) {
        Stop-Replay 2 "RECORD REPLAY ERROR: the session did not give a recording" @($demo | ForEach-Object { "  $_" })
    }
}
$dir = Join-Path $RepoRoot "Saved\Recordings\$recordName"
$replayFile = Join-Path $dir "$recordName.json"
$seen = Read-JsonFile (Join-Path $dir "$recordName.session.json")
if (-not $seen -or -not (Test-Path $replayFile)) {
    Stop-Replay 2 "RECORD REPLAY ERROR: $dir has no recording with its scenario and its session file"
}

$script:replayLines = @()
function Invoke-Replay {
    $script:replayLines = @(& powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'scenario.ps1') -File $replayFile)
    $metricsOfRun = Read-JsonFile (Join-Path $RepoRoot "Saved\Scenarios\$recordName\metrics.json")
    if (-not $metricsOfRun -or -not $metricsOfRun.ok) { return $null }
    return $metricsOfRun
}
function Stop-NoReplay {
    Stop-Replay 2 "RECORD REPLAY ERROR: the replay did not run" @($script:replayLines | Select-Object -First 5 | ForEach-Object { "  $_" })
}
$replay = Invoke-Replay
if (-not $replay) { Stop-NoReplay }

$rows = @()
$bad = 0
$shown = 0
# The walk (-Walk) shoves the character for seconds: a session with a shove is not reproduced by its replay after the
# shove (docs/pitfalls.md, "A session with hard blows..."). What holds by design is judged (the first contact of each
# effector, and two replays alike); the outcome after the shove is shown and not judged.
$judgeOutcome = -not $Walk
function Add-Row([string]$What, [string]$Session, [string]$Replayed, [bool]$Same, [bool]$Judged = $true) {
    $mark = 'ok  '
    if (-not $Same) {
        if ($Judged) { $mark = 'FAIL'; $script:bad++ } else { $mark = 'info'; $script:shown++ }
    }
    $script:rows += ("  [{0}] {1,-34} session {2,-28} replay {3}" -f $mark, $What, $Session, $Replayed)
}

# The body's states, and the frame each began in.
$seenStates = @($seen.states)
$replayStates = @($replay.control.states)
$seenNames = ($seenStates | ForEach-Object { $_.state }) -join '>'
$replayNames = ($replayStates | ForEach-Object { $_.state }) -join '>'
Add-Row 'states of the body' $seenNames $replayNames ($seenNames -eq $replayNames) $judgeOutcome
if ($seenNames -eq $replayNames) {
    $worst = 0
    for ($i = 0; $i -lt $seenStates.Count; $i++) {
        $off = [Math]::Abs([int]$seenStates[$i].frame - [int]$replayStates[$i].frame)
        if ($off -gt $worst) { $worst = $off }
    }
    $seenFrames = ($seenStates | ForEach-Object { $_.frame }) -join ','
    $replayFrames = ($replayStates | ForEach-Object { $_.frame }) -join ','
    Add-Row "their frames (within $Frames)" $seenFrames $replayFrames ($worst -le $Frames) $judgeOutcome
}

# Where the pelvis ended.
$pelvis = @($replay.body.bodies | Where-Object { $_.bone -eq 'pelvis' })[0]
if ($pelvis -and $seen.pelvis_cm) {
    $a = $seen.pelvis_cm
    $b = $pelvis.final_location_cm
    $apart = [Math]::Sqrt([Math]::Pow($a[0] - $b[0], 2) + [Math]::Pow($a[1] - $b[1], 2) + [Math]::Pow($a[2] - $b[2], 2))
    Add-Row "pelvis (within $PelvisCm cm)" ('{0:N2},{1:N2},{2:N2}' -f $a[0], $a[1], $a[2]) ('{0:N2},{1:N2},{2:N2}; {3:N3} cm apart' -f $b[0], $b[1], $b[2], $apart) ($apart -le $PelvisCm) $judgeOutcome
} else {
    Add-Row 'pelvis' 'none' 'none' $false
}

# What each hand touched, and with how much.
foreach ($hand in $seen.hands.PSObject.Properties) {
    $mine = $hand.Value
    $theirs = $replay.effectors.list.($hand.Name)
    if (-not $theirs) {
        Add-Row $hand.Name 'recorded' 'not in the replay' $false
        continue
    }
    # In the walk the hands touch the character only after the body's shove has begun: their first touch is its outcome.
    $firstJudged = $judgeOutcome -or $hand.Name -eq 'body'
    Add-Row "$($hand.Name) first touched" "$($mine.first_touched)" "$($theirs.first_touched)" ("$($mine.first_touched)" -eq "$($theirs.first_touched)") $firstJudged
    $base = [Math]::Max([Math]::Abs([double]$mine.contact_impulse_ns), 1.0e-6)
    $pct = 100.0 * [Math]::Abs([double]$mine.contact_impulse_ns - [double]$theirs.contact_impulse_ns) / $base
    if ([double]$mine.contact_impulse_ns -eq 0 -and [double]$theirs.contact_impulse_ns -eq 0) { $pct = 0 }
    Add-Row "$($hand.Name) impulse (within $ImpulsePct%)" ('{0:N2} N*s' -f $mine.contact_impulse_ns) ('{0:N2} N*s; {1:N2}% off' -f $theirs.contact_impulse_ns, $pct) ($pct -le $ImpulsePct) $judgeOutcome
    Add-Row "$($hand.Name) arrived at" ('{0:N2} m/s' -f $mine.first_contact_speed_mps) ('{0:N2} m/s' -f $theirs.first_contact_speed_mps) ([Math]::Abs([double]$mine.first_contact_speed_mps - [double]$theirs.first_contact_speed_mps) -le 0.1) $firstJudged
}
if ($Walk) {
    $hasBody = @($seen.hands.PSObject.Properties | Where-Object { $_.Name -eq 'body' }).Count -eq 1
    Add-Row "the player's body is recorded" "$hasBody" "$([bool]$replay.effectors.list.body)" ($hasBody -and [bool]$replay.effectors.list.body)
}
$touched = @($seen.hands.PSObject.Properties | Where-Object { [double]$_.Value.contact_impulse_ns -gt 0 }).Count
if ($touched -eq 0) {
    Add-Row 'a hand touched something' 'no' '' $false
}

$again = ''
if ($Twice) {
    $second = Invoke-Replay
    if (-not $second) { Stop-NoReplay }
    $sameHashes = $second.hashes.bodies -eq $replay.hashes.bodies -and $second.hashes.bones -eq $replay.hashes.bones
    Add-Row 'two replays, hashes' "$($replay.hashes.bodies) $($replay.hashes.bones)" "$($second.hashes.bodies) $($second.hashes.bones)" $sameHashes
}

$verdict = 'OK'
if ($bad -gt 0) { $verdict = 'FAILED' }
$notJudged = ''
if ($shown -gt 0) { $notJudged = "; $shown more differ after the shove and are shown, not judged" }
$line = ("RECORD REPLAY {0}: '{1}', {2} frames of the hands from frame {3} of the body's life; {4} of {5} comparisons hold{6}; hashes bodies={7} bones={8}" -f
    $verdict, $recordName, $seen.frames, $seen.start_frame, ($rows.Count - $shown - $bad), ($rows.Count - $shown), $notJudged, $replay.hashes.bodies, $replay.hashes.bones)
$more = @($rows) + @("  replay scenario: $replayFile")
if ($bad -gt 0) { Stop-Replay 1 $line $more }
Stop-Replay 0 $line $more
