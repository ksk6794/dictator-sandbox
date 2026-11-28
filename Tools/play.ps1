# Opens a scenario's scene as an interactive session: the desktop pawn with its hands and tools.
#   powershell -ExecutionPolicy Bypass -File Tools\play.ps1                 the test room with the standing NPC
#   ... -Name hit_chest_200_falls   any scenario: its map, body and camera are used, its events are not
#   ... -File <path>                a scenario file from anywhere
#   ... -Tune "effectors.hand.max_force_n=600"   tuning values for this session only
#   ... -TuningDir <dir>            read the tuning files from this directory instead of Config\Tuning
#   ... -Overlay all                start with the debug overlays on (all, or layers by name: "com,support,
#                                   joints,assist,contacts,state,hands"). In the session the V key switches
#                                   them, and the console command Sandbox.Overlay chooses layers
#   ... -ResX 1600 -ResY 900        window size
#   ... -Smoke                      no window and no person: the session uses every tool itself, checks the
#                                   result and exits (about 15 s). Writes <Out>\play.json
#   ... -Smoke -Shot                the same, rendered offscreen, with <Out>\final.png of the last frame
#   ... -Record <name>              the hands are recorded from the first frame until the session ends (the K
#                                   key ends a recording and starts another). Written to Saved\Recordings\<name>:
#                                   the recording, a scenario that replays it (Tools\scenario.ps1 -File ...) and
#                                   what the session saw of its outcome
#   ... -Demo                       no window and no person: the session uses the hands alone and records them
#                                   (Saved\Recordings\<Record or "demo_hands">). Tools\record-replay.ps1 replays it
#   ... -Seconds 180                the session ends by itself after that many simulated seconds, as Escape
#                                   would end it: a recording is written. (A session that is closed by its
#                                   window, killed or timed out loses its recording.)
#   ... -Grab                       a session with the grab switched on whatever the tuning says (effectors.hand.grab=1;
#                                   it is 1 in the tuning files since 25 November 2026, so every session has it). On the
#                                   desktop the G key holds the hands open: reach, let G go while the hand presses on
#                                   the body, and the hand holds it; G again lets go
#   ... -Perception                 the body sees what moves near it (N5): the session tells it the hands, the foot and
#                                   the things every frame, and gaze, anticipation and the guarding arm are switched on
#                                   (perception.gaze, anticipate, guard = 1; 0 in the tuning files). A recording's
#                                   replay judges what moves with the same switches
#   ... -Demo -Grab                 the same for the grab (effectors.hand.grab switched on): the hands open, the right
#                                   one reaches into the chest and its grip takes hold; the character is put back
#                                   while it is held; the hand takes hold of the new one, pulls and lets go; then
#                                   the desktop's kick (the F key) at the chest
#   ... -Demo -Takes                the same in three takes: the character is put back before each (the N key of
#                                   a session; in the headset both grips held for a second), and each take is a
#                                   recording of its own (Saved\Recordings\demo_takes, demo_takes_2, demo_takes_3)
#   ... -Demo -Walk                 the way a person in the headset takes, without one or a headset: a made-up headset
#                                   stands in (the head off the middle of its play space, the controllers by the hips);
#                                   the head is put at arm's length, the right stick walks into the character, presses
#                                   on and slides along it, the left stick turns right and back, the stick walks away;
#                                   each is checked (FR-42) and the hands and the player's body are recorded
#                                   (Saved\Recordings\demo_walk). Tools\record-replay.ps1 -Walk replays it
#   ... -VR                         in the headset (headset on, SteamVR running): the same session, the hands
#                                   follow the controllers' grip poses, the grip is the squeeze, the headset paces
#                                   the frames. The player stands at arm's length in front of the character; the
#                                   window shows what the headset sees. With -Record the hands are recorded as on
#                                   the desktop. Escape in the window ends the session. Not with -Smoke or -Demo
# Keys are listed on the screen and in Source\SandboxInteraction\Public\SandboxPawn.h. Escape quits.
# Close the Unreal Editor first. Exit code: 0 the session ended (or the self-test passed), 1 a self-test
# check failed, 2 the session could not run.
param(
    [string]$Name = 'room_stand_npc',
    [string]$File,
    [string]$Tune,
    [string]$TuningDir,
    [string]$Overlay,
    [int]$ResX = 1600,
    [int]$ResY = 900,
    [switch]$Smoke,
    [switch]$Demo,
    [switch]$Takes,
    [switch]$Walk,
    [switch]$Grab,
    [switch]$Perception,
    [switch]$VR,
    [double]$Seconds = 0,
    [string]$Record,
    [switch]$Shot,
    [string]$Out,
    [int]$TimeoutSec = 0
)
. "$PSScriptRoot\common.ps1"

if ($File) {
    $scenarioFile = [System.IO.Path]::GetFullPath($File)
    $Name = [System.IO.Path]::GetFileNameWithoutExtension($scenarioFile)
} else {
    $scenarioFile = Join-Path $RepoRoot "Scenarios\$Name.json"
}
if (-not (Test-Path $scenarioFile)) {
    Write-Output "PLAY ERROR: $scenarioFile does not exist"
    exit 2
}
$spec = Read-JsonFile $scenarioFile
$fps = 90
if ($spec.fixed_fps) { $fps = [int]$spec.fixed_fps }

if (-not $Out) { $Out = Join-Path $RepoRoot "Saved\Play\$Name" }
$Out = [System.IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force $Out | Out-Null
$resultFile = Join-Path $Out 'play.json'
$logFile = Join-Path $Out 'run.log'
# The script's own verdict as a file (Tools\bootstrap.ps1 decides by it): the session's play.json does not hold what the log
# adds to it (errors of LogSandbox, warnings of Physics Control).
$verdictFile = Join-Path $Out 'play-result.json'
foreach ($stale in @('play.json', 'final.png', 'play-result.json')) {
    $path = Join-Path $Out $stale
    if (Test-Path $path) { Remove-Item $path -Force }
}
function Stop-Play([int]$Code, [string]$Line, [string[]]$More = @()) {
    Write-JsonFile $verdictFile ([ordered]@{ ok = ($Code -eq 0); exit_code = $Code; verdict = $Line })
    Write-Output $Line
    $More | ForEach-Object { Write-Output $_ }
    exit $Code
}

# -Grab is the grab switched on (effectors.hand.grab), whatever else is tuned (it is on in the tuning files since
# 25 November 2026; the flag stays for the grab's own demo and for a run with other tuning): for that demo, and for
# a person's session, on the desktop (the G key holds the hands open) and in the headset.
if ($Grab) {
    if ($Tune) { $Tune = "$Tune,effectors.hand.grab=1" } else { $Tune = 'effectors.hand.grab=1' }
}
# -Perception: what the body sees and does with it (N5), for a person's look; the tuning files keep it off.
if ($Perception) {
    $seeing = 'perception.gaze=1,perception.anticipate=1,perception.guard=1'
    if ($Tune) { $Tune = "$seeing,$Tune" } else { $Tune = $seeing }
}
$arguments = "-game -Play=`"$scenarioFile`" -Out=`"$Out`" -UseFixedTimeStep -FPS=$fps"
if ($Perception) { $arguments += ' -PlayPerception' }
if ($spec.map) { $arguments = "$($spec.map) $arguments" }
if ($Tune) { $arguments += " -Tune=`"$Tune`"" }
if ($TuningDir) { $arguments += " -TuningDir=`"$([System.IO.Path]::GetFullPath($TuningDir))`"" }
if ($Overlay) { $arguments += " -Overlay=`"$Overlay`"" }
if ($VR) {
    $blocker = Get-HeadsetBlocker
    if ($blocker) {
        Stop-Play 2 "PLAY ERROR: the session in the headset was not started. $blocker."
    }
    # No pool beside a session in the headset (decision 2 of docs/fast-checks.md).
    $poolWhy = Stop-Pool
    if ($poolWhy) { Stop-Play 2 "PLAY ERROR: the session in the headset was not started: the pool did not stop: $poolWhy." }
    $state = Get-HeadsetState
    if ($state -and $state -ne 'Ready') { Write-Output "PLAY NOTE: SteamVR reports '$state': the headset is not being worn." }
}
if ($VR -and ($Demo -or $Smoke)) {
    Stop-Play 2 'PLAY ERROR: -VR is a session with a person in the headset; -Smoke and -Demo run without one'
}
if ($Demo -and -not $Grab -and -not $Record) { $Record = 'demo_hands'; if ($Takes) { $Record = 'demo_takes' } elseif ($Walk) { $Record = 'demo_walk' } }
if ($Record) {
    if ($Record -notmatch '^[A-Za-z0-9_\-]+$') {
        Stop-Play 2 "PLAY ERROR: -Record takes a name of letters, digits, '_' and '-', not '$Record'"
    }
    $arguments += " -Record=$Record"
}

if ($Demo) {
    if ($Grab) { $arguments += ' -PlayDemoGrab -nullrhi' } elseif ($Takes) { $arguments += ' -PlayDemoTakes -nullrhi' } elseif ($Walk) { $arguments += ' -PlayDemoWalk -nullrhi' } else { $arguments += ' -PlayDemo -nullrhi' }
    if ($TimeoutSec -le 0) { $TimeoutSec = 300 }
    $run = Invoke-Editor -ArgumentString $arguments -LogFile $logFile -TimeoutSec $TimeoutSec -FastStart
    $result = Read-JsonFile $resultFile
    if ($run.TimedOut -or -not $result -or -not $result.ok) {
        $why = "it wrote no play.json (process exit code $($run.ExitCode))"
        if ($run.TimedOut) { $why = "it timed out after $TimeoutSec s" }
        if ($result -and -not $result.ok) { $why = "the session could not start: $($result.error)" }
        Stop-Play 2 "PLAY ERROR: the hands' session did not run: $why. Log: $logFile" @(Get-LogErrors $logFile | ForEach-Object { "  $_" })
    }
    $errors = @(Get-LogErrors $logFile | Where-Object { $_ -match 'LogSandbox' })
    if ($Grab) {
        $held = $result.grab
        $told = "took hold $($held.held), lost what it held when the character was put back $($held.lost_on_reset), took hold of the new one $($held.held_again), let go $($held.let_go); $($held.grabs) grab(s), $($held.grabs_lost) lost, $($held.grab_slips) slipped, the joint carried $([Math]::Round([double]$held.max_grab_force_n)) N at most, the hand went $([Math]::Round([double]$held.max_speed_mps, 1)) m/s at most; the kick landed $($held.kick_landed) ($([Math]::Round([double]$held.kick_impulse_ns)) N*s from the foot after the key, and a hit told by the character); the character ends $($held.character_state_at_end)"
        if (-not $result.pass -or $errors.Count -gt 0) {
            Stop-Play 1 "PLAY GRAB FAILED on '$Name': $told" @($errors | ForEach-Object { "  log: $_" })
        }
        Stop-Play 0 "PLAY GRAB OK on '$Name': $told"
    }
    if ($Walk) {
        $moved = $result.walk
        $checks = @($moved.checks)
        $failed = @($checks | Where-Object { -not $_.pass })
        $verdict = 'OK'
        if (-not $result.pass -or $errors.Count -gt 0 -or $checks.Count -eq 0) { $verdict = 'FAILED' }
        $line = ("PLAY WALK {0} on '{1}': {2} of {3} checks passed; walked {4:N2} m at {5:N2} m/s at most, {6} snap turn(s) to the left and {7} to the right; the character ends {8}" -f
            $verdict, $Name, ($checks.Count - $failed.Count), $checks.Count, [double]$moved.walked_m, [double]$moved.max_speed_mps, $moved.snaps_left, $moved.snaps_right, $moved.character_state_at_end)
        $more = @()
        foreach ($check in $checks) {
            $mark = 'ok  '
            if (-not $check.pass) { $mark = 'FAIL' }
            $more += ("  [{0}] {1}: {2}" -f $mark, $check.name, $check.detail)
        }
        if ($result.error) { $more += "  error: $($result.error)" }
        $more += @($errors | ForEach-Object { "  log: $_" })
        $more += "  replay: $($result.replay_scenario)"
        if ($verdict -eq 'OK') { Stop-Play 0 $line $more }
        Stop-Play 1 $line $more
    }
    if (-not $result.pass -or $errors.Count -gt 0) {
        Stop-Play 1 "PLAY DEMO FAILED: no recording of the hands on '$Name': $($result.error)" @($errors | ForEach-Object { "  log: $_" })
    }
    Stop-Play 0 "PLAY DEMO OK: the hands' session on '$Name' recorded ($($result.frames) frames of session)" @("  replay: $($result.replay_scenario)")
}

if ($Smoke) {
    $arguments += ' -PlaySmoke'
    if ($Shot) {
        $arguments += " -RenderOffscreen -windowed -ResX=1280 -ResY=720 -Shot=`"$(Join-Path $Out 'final.png')`""
    } else {
        $arguments += ' -nullrhi'
    }
    if ($TimeoutSec -le 0) { $TimeoutSec = 300 }
    $run = Invoke-Editor -ArgumentString $arguments -LogFile $logFile -TimeoutSec $TimeoutSec -FastStart
    $result = Read-JsonFile $resultFile
    if ($run.TimedOut) {
        Stop-Play 2 "PLAY ERROR: the self-test timed out after $TimeoutSec s. Log: $logFile"
    }
    if (-not $result) {
        Stop-Play 2 "PLAY ERROR: the self-test wrote no play.json (process exit code $($run.ExitCode)). Log: $logFile" @(Get-LogErrors $logFile | ForEach-Object { "  $_" })
    }
    if (-not $result.ok) {
        Stop-Play 2 "PLAY ERROR: the session could not start: $($result.error)"
    }
    $failed = @($result.checks | Where-Object { -not $_.pass })
    # A run can pass its own checks and still have logged errors (a hand that was not created, say), or
    # warnings of Physics Control: muscles that found no target to pull to say so there and nowhere else.
    $errors = @(Get-LogErrors $logFile | Where-Object { $_ -match 'LogSandbox' })
    if (Test-Path $logFile) {
        $controlWarnings = @(Select-String -Path $logFile -Pattern 'LogPhysicsControl: Warning')
        if ($controlWarnings.Count -gt 0) {
            $errors += "$($controlWarnings.Count) warning(s) of Physics Control, the first: $($controlWarnings[0].Line.Trim())"
        }
    }
    $verdict = 'OK'
    if ($failed.Count -gt 0 -or $errors.Count -gt 0) { $verdict = 'FAILED' }
    $line = ("PLAY SMOKE {0}: {1} of {2} checks passed on '{3}' ({4} frames)" -f $verdict, ($result.checks.Count - $failed.Count), $result.checks.Count, $Name, $result.frames)
    $more = @()
    foreach ($check in $result.checks) {
        $mark = 'ok  '
        if (-not $check.pass) { $mark = 'FAIL' }
        $more += ("  [{0}] {1}: {2}" -f $mark, $check.name, $check.detail)
    }
    $more += @($errors | ForEach-Object { "  log: $_" })
    $more += "  result: $resultFile"
    if ($verdict -eq 'OK') { Stop-Play 0 $line $more }
    Stop-Play 1 $line $more
}

# A person's session: a window, the frame rate held to the simulation's (in the headset: by the headset), until
# the window is closed.
$arguments += " -windowed -ResX=$ResX -ResY=$ResY"
if ($VR) { $arguments += ' -vr' }
if ($Seconds -gt 0) { $arguments += (' -PlaySeconds={0}' -f $Seconds.ToString([System.Globalization.CultureInfo]::InvariantCulture)) }
if ($TimeoutSec -le 0) { $TimeoutSec = 6 * 3600 }
# Without the Zen server and the platform check (SP-2), but in the headset.
$run = Invoke-Editor -ArgumentString $arguments -LogFile $logFile -TimeoutSec $TimeoutSec -VR:$VR -FastStart:(-not $VR)
$errors = @(Get-LogErrors $logFile | Where-Object { $_ -match 'LogSandbox' })
if ($errors.Count -gt 0) {
    Write-Output "PLAY ERROR: the session on '$Name' logged errors. Log: $logFile"
    $errors | ForEach-Object { Write-Output "  $_" }
    exit 2
}
# A session that crashed is not one that ended: the engine's own errors are not LogSandbox's, and the first
# crash in the headset was reported as "PLAY ENDED".
if (Test-Path $logFile) {
    # A call stack alone is no crash: the engine prints one for an ensure that it survives.
    $fatal = @(Select-String -Path $logFile -Pattern 'Fatal error!|Unhandled Exception|=== Critical error: ===' | Select-Object -First 1)
    $crash = @()
    if ($fatal.Count -gt 0) { $crash = @(Select-String -Path $logFile -Pattern 'Fatal error!|Unhandled Exception|\[Callstack\]' | Select-Object -First 4) }
    if ($crash.Count -gt 0) {
        Write-Output "PLAY ERROR: the session on '$Name' crashed (exit code $($run.ExitCode)); a recording that was running is lost. Log: $logFile"
        $crash | ForEach-Object { Write-Output ("  " + ($_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '')) }
        exit 2
    }
}
$where = 'session'
if ($VR) { $where = 'session in the headset' }
Write-Output "PLAY ENDED: $where on '$Name' (exit code $($run.ExitCode)). Log: $logFile"
if ($Record -and (Test-Path $logFile)) {
    # Asked to record: say what was written, or that nothing was.
    $saved = @(Select-String -Path $logFile -Pattern 'frames of the hands recorded')
    if ($saved.Count -eq 0) {
        Write-Output "  NO RECORDING was written for -Record $Record (a session records until Escape or -Seconds ends it)"
    }
    $saved | ForEach-Object { Write-Output ("  " + ($_.Line -replace '^.*LogSandbox: (Display: )?', '')) }
}
if ($VR -and (Test-Path $logFile)) {
    # What the tracking gave: which controllers were tracked, whether the runtime gave their velocities, and
    # how fast the session's 90 frames a second really went by.
    Select-String -Path $logFile -Pattern 'LogSandbox: (Display: )?(Headset:|The pawn is in a headset|Play session: in the headset|Play session: \d+ frames in)' |
        ForEach-Object { Write-Output ("  " + ($_.Line -replace '^.*LogSandbox: (Display: )?', '')) }
}
exit 0
