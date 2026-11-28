# Runs one scenario and reports its metrics.
#   powershell -ExecutionPolicy Bypass -File Tools\scenario.ps1 -Name drop_passive
#   ... -File <path>     run a scenario file from anywhere instead of Scenarios\<Name>.json (generated scenarios)
#   ... -Rendered        render offscreen with the GPU instead of running without a renderer
#   ... -Shot            also save <Out>\final.png (implies -Rendered)
#   ... -ShotAt "3.05,3.2"  with -Shot: also a picture at these simulated times, <Out>\shot_3.050.png and so on
#   ... -Overlay all     debug overlays in the pictures: all, or layers by name, "com,support,joints,assist,
#                        contacts,state,hands". Rendered runs only; a run without a renderer ignores it
#   ... -VR              run in the headset through OpenXR (headset on, runtime running); frames are paced by
#                        the headset, so the run takes its simulated duration in real time
#   ... -CVars "a=1,b=2" console variables for this run only (applied at start-up as device-profile overrides)
#   ... -Tune "muscles.legs.stiffness_nm_per_rad=400,solver.position_iterations=32"
#                        tuning values for this run only, on top of the files in Config\Tuning (active control);
#                        the values used are written to metrics.json under control.tuning
#   ... -TuningDir <dir> read the tuning files from this directory instead of Config\Tuning
#   ... -Out <dir>       output directory (default Saved\Scenarios\<name>)
# Prints a one-line verdict plus the checks and writes <Out>\metrics.json, <Out>\trace.json and, for a
# simulated body, <Out>\series.csv (energy, balance and penetration of every frame); the run's log lines go to
# <Out>\run.log, the engine process's whole log to <Out>\process.log.
# Exit code: 0 pass, 1 a check failed, 2 the scenario could not run.
param(
    [string]$Name,
    [string]$File,
    [switch]$Rendered,
    [switch]$VR,
    [string]$CVars,
    [string]$Tune,
    [string]$TuningDir,
    [switch]$Shot,
    [string]$ShotAt,
    [string]$Overlay,
    [string]$Out,
    [int]$TimeoutSec = 900,
    [switch]$Quiet
)
. "$PSScriptRoot\common.ps1"

if ($File) {
    $scenarioFile = [System.IO.Path]::GetFullPath($File)
    $Name = [System.IO.Path]::GetFileNameWithoutExtension($scenarioFile)
} elseif ($Name) {
    $scenarioFile = Join-Path $RepoRoot "Scenarios\$Name.json"
} else {
    Write-Output 'SCENARIO ERROR: give -Name <scenario> or -File <path>'
    exit 2
}
if (-not (Test-Path $scenarioFile)) {
    Write-Output "SCENARIO ERROR: $scenarioFile does not exist"
    exit 2
}
$spec = Read-JsonFile $scenarioFile
$fps = 90
if ($spec.fixed_fps) { $fps = [int]$spec.fixed_fps }

if (-not $Out) { $Out = Join-Path $RepoRoot "Saved\Scenarios\$Name" }
$Out = [System.IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force $Out | Out-Null
$metricsFile = Join-Path $Out 'metrics.json'
$logFile = Join-Path $Out 'run.log'

# The run goes through a queue of one job, as the suite's runs do (Get-QueueArguments): the engine runs a short
# warm-up first, so that this run gives the same hashes as the same scenario in a suite, a matrix or a sweep. The
# queue removes stale results, so an old pass can never be mistaken for this run. The engine's whole log goes to
# process.log next to the job's run.log.
if ($Shot) { $Rendered = $true }
$job = @{ File = $scenarioFile; Out = $Out; Tune = $Tune; TuningDir = $TuningDir }
if ($Shot) { $job.Shot = Join-Path $Out 'final.png' }
if ($Shot -and $ShotAt) { $job.ShotAt = $ShotAt }
$queue = New-ScenarioQueue -Jobs @($job)
$processLog = Join-Path $Out 'process.log'
$arguments = Get-QueueArguments -Queue $queue -Fps $fps -Headset:$VR
if ($VR) {
    $blocker = Get-HeadsetBlocker
    if ($blocker) {
        Write-Output "SCENARIO ERROR: the run in the headset was not started. $blocker."
        exit 2
    }
    # No pool beside a run in the headset (decision 2 of docs/fast-checks.md).
    $poolWhy = Stop-Pool
    if ($poolWhy) { Write-Output "SCENARIO ERROR: the run in the headset was not started: the pool did not stop: $poolWhy."; exit 2 }
    $arguments += ' -vr -windowed -ResX=1280 -ResY=720'
} elseif ($Rendered) {
    $arguments += ' -RenderOffscreen -windowed -ResX=1280 -ResY=720'
} else {
    $arguments += ' -nullrhi'
}
if ($CVars) { $arguments += " -dpcvars=`"$CVars`"" }
if ($Overlay) { $arguments += " -Overlay=`"$Overlay`"" }

# A running pool takes a headless run (Tools\pool.ps1; SP-4): the same warm-up and the same hashes, without the engine's start.
# The pool's processes run at 90 frames a second: a scenario at another rate keeps a process of its own.
$poolDir = $null
if (-not $VR -and -not $Rendered -and -not $CVars -and -not $Overlay -and $fps -eq 90) { $poolDir = Get-LivePool }
if ($poolDir) {
    Remove-Item $queue -Recurse -Force -ErrorAction SilentlyContinue
    $poolStarted = Get-Date
    $poolNotes = @(Invoke-ScenarioBatch -Jobs @($job) -Parallel 1 -TimeoutSec $TimeoutSec)
    foreach ($note in $poolNotes) { [Console]::Error.WriteLine($note) }
    # The job's own log; a crash of the process itself is in the pool's process logs, which hold every job they ran.
    $processLog = Join-Path $Out 'run.log'
    # Past its time the job is given up and its process ended (Submit-CheckJobs).
    $timedOut = $script:LastBatchLost -gt 0 -and ((Get-Date) - $poolStarted).TotalSeconds -ge $TimeoutSec
    if ($timedOut) { $metrics = $null; Remove-Item $metricsFile -Force -ErrorAction SilentlyContinue }
    $run = [pscustomobject]@{ TimedOut = $timedOut; ExitCode = 0 }
} else {
    # Without the Zen server and the platform check (SP-2), but in the headset.
    $run = Invoke-Editor -ArgumentString $arguments -LogFile $processLog -TimeoutSec $TimeoutSec -VR:$VR -FastStart:(-not $VR)
}
$metrics = Read-JsonFile $metricsFile
if (@(Get-ChildItem $queue -Filter '*.json' -File -ErrorAction SilentlyContinue).Count -eq 0) { Remove-Item $queue -Recurse -Force -ErrorAction SilentlyContinue }

if ($run.TimedOut) {
    Write-Output "SCENARIO ERROR: $Name timed out after $TimeoutSec s. Log: $processLog"
    exit 2
}
if (-not $metrics) {
    Write-Output "SCENARIO ERROR: $Name wrote no metrics.json (process exit code $($run.ExitCode)). Log: $processLog"
    Get-LogErrors $processLog | ForEach-Object { Write-Output "  $_" }
    if ($poolDir) { Write-Output "  It was handed to the pool: a crash of its process is in $(Join-Path $poolDir 'process_*.log') (or, if the pool was ending, under Saved\Queue)" }
    exit 2
}
if (-not $metrics.ok) {
    Write-Output "SCENARIO ERROR: $Name could not run: $($metrics.error)"
    exit 2
}

# A console variable the engine does not know is accepted without a word and changes nothing: the run then
# looks like proof that the variable makes no difference. The engine's log says so; pass it on.
if ($CVars) {
    foreach ($assignment in $CVars.Split(',')) {
        $variable = $assignment.Split('=')[0].Trim()
        if ($variable -and (Select-String -Path $processLog -SimpleMatch "CVar [[${variable}:" | Where-Object { $_.Line -match 'dummy variable created' })) {
            Write-Output "SCENARIO WARNING: the engine knows no console variable '$variable'; -CVars set nothing by that name"
        }
    }
}

$verdict = 'FAIL'
if ($metrics.pass) { $verdict = 'PASS' }
$mode = 'headless'
if ($metrics.rendered) { $mode = 'rendered' }
if ($metrics.xr -and $metrics.xr.stereo) { $mode = 'VR' }
Write-Output ("SCENARIO {0}: {1} ({2}, {3} frames, {4:N1} s simulated in {5:N1} s, hashes bodies={6} bones={7})" -f `
    $verdict, $Name, $mode, $metrics.timing.frames, $metrics.timing.sim_time_s, $metrics.timing.wall_time_s, $metrics.hashes.bodies, $metrics.hashes.bones)
if ($metrics.performance) {
    $perf = $metrics.performance
    Write-Output ("  frame times over {0} frames, ms (mean / p99): gpu {1:N2} / {2:N2}, game thread {3:N2} / {4:N2}, render thread {5:N2} / {6:N2}, interval {7:N2} / {8:N2}" -f `
        $perf.frames, $perf.gpu_ms.mean, $perf.gpu_ms.p99, $perf.game_thread_ms.mean, $perf.game_thread_ms.p99, $perf.render_thread_ms.mean, $perf.render_thread_ms.p99, $perf.frame_interval_ms.mean, $perf.frame_interval_ms.p99)
}
if ($metrics.xr -and $metrics.xr.stereo) {
    Write-Output ("  headset: {0} {1}, render target {2} x {3}" -f $metrics.xr.system, $metrics.xr.version, $metrics.xr.render_target_width, $metrics.xr.render_target_height)
}
if ($metrics.performance -and $metrics.renderer) {
    $r = $metrics.renderer
    Write-Output ("  renderer: forward={0} instanced_stereo={1} hmd_target_percent={2} aa={3} gi={4} reflections={5} vsm={6}" -f `
        $r.'r.ForwardShading', $r.'vr.InstancedStereo', $r.'xr.SecondaryScreenPercentage.HMDRenderTarget', $r.'r.AntiAliasingMethod', $r.'r.DynamicGlobalIlluminationMethod', $r.'r.ReflectionMethod', $r.'r.Shadow.Virtual.Enable')
}
if (-not $Quiet) {
    foreach ($check in $metrics.checks) {
        $mark = 'ok  '
        if (-not $check.pass) { $mark = 'FAIL' }
        Write-Output ("  [{0}] {1}: value={2} limit={3}" -f $mark, $check.name, $check.value, $check.limit)
    }
    Write-Output "  metrics: $metricsFile"
}

if ($metrics.pass) { exit 0 }
exit 1
