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
foreach ($stale in @('play.json', 'final.png')) {
    $path = Join-Path $Out $stale
    if (Test-Path $path) { Remove-Item $path -Force }
}

$arguments = "-game -Play=`"$scenarioFile`" -Out=`"$Out`" -UseFixedTimeStep -FPS=$fps"
if ($spec.map) { $arguments = "$($spec.map) $arguments" }
if ($Tune) { $arguments += " -Tune=`"$Tune`"" }
if ($TuningDir) { $arguments += " -TuningDir=`"$([System.IO.Path]::GetFullPath($TuningDir))`"" }
if ($Overlay) { $arguments += " -Overlay=`"$Overlay`"" }

if ($Smoke) {
    $arguments += ' -PlaySmoke'
    if ($Shot) {
        $arguments += " -RenderOffscreen -windowed -ResX=1280 -ResY=720 -Shot=`"$(Join-Path $Out 'final.png')`""
    } else {
        $arguments += ' -nullrhi'
    }
    if ($TimeoutSec -le 0) { $TimeoutSec = 300 }
    $run = Invoke-Editor -ArgumentString $arguments -LogFile $logFile -TimeoutSec $TimeoutSec
    $result = Read-JsonFile $resultFile
    if ($run.TimedOut) {
        Write-Output "PLAY ERROR: the self-test timed out after $TimeoutSec s. Log: $logFile"
        exit 2
    }
    if (-not $result) {
        Write-Output "PLAY ERROR: the self-test wrote no play.json (process exit code $($run.ExitCode)). Log: $logFile"
        Get-LogErrors $logFile | ForEach-Object { Write-Output "  $_" }
        exit 2
    }
    if (-not $result.ok) {
        Write-Output "PLAY ERROR: the session could not start: $($result.error)"
        exit 2
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
    Write-Output ("PLAY SMOKE {0}: {1} of {2} checks passed on '{3}' ({4} frames)" -f $verdict, ($result.checks.Count - $failed.Count), $result.checks.Count, $Name, $result.frames)
    foreach ($check in $result.checks) {
        $mark = 'ok  '
        if (-not $check.pass) { $mark = 'FAIL' }
        Write-Output ("  [{0}] {1}: {2}" -f $mark, $check.name, $check.detail)
    }
    $errors | ForEach-Object { Write-Output "  log: $_" }
    Write-Output "  result: $resultFile"
    if ($verdict -eq 'OK') { exit 0 }
    exit 1
}

# A person's session: a window, the frame rate held to the simulation's, until the window is closed.
$arguments += " -windowed -ResX=$ResX -ResY=$ResY"
if ($TimeoutSec -le 0) { $TimeoutSec = 6 * 3600 }
$run = Invoke-Editor -ArgumentString $arguments -LogFile $logFile -TimeoutSec $TimeoutSec
$errors = @(Get-LogErrors $logFile | Where-Object { $_ -match 'LogSandbox' })
if ($errors.Count -gt 0) {
    Write-Output "PLAY ERROR: the session on '$Name' logged errors. Log: $logFile"
    $errors | ForEach-Object { Write-Output "  $_" }
    exit 2
}
Write-Output "PLAY ENDED: session on '$Name' (exit code $($run.ExitCode)). Log: $logFile"
exit 0
