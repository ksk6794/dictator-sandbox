# Runs one scenario and reports its metrics.
#   powershell -ExecutionPolicy Bypass -File Tools\scenario.ps1 -Name drop_passive
#   ... -File <path>     run a scenario file from anywhere instead of Scenarios\<Name>.json (generated scenarios)
#   ... -Rendered        render offscreen with the GPU instead of running without a renderer
#   ... -Shot            also save <Out>\final.png (implies -Rendered)
#   ... -VR              run in the headset through OpenXR (headset on, runtime running); frames are paced by
#                        the headset, so the run takes its simulated duration in real time
#   ... -CVars "a=1,b=2" console variables for this run only (applied at start-up as device-profile overrides)
#   ... -Tune "muscles.legs.stiffness_nm_per_rad=400,solver.position_iterations=32"
#                        tuning values for this run only, on top of the files in Config\Tuning (active control);
#                        the values used are written to metrics.json under control.tuning
#   ... -TuningDir <dir> read the tuning files from this directory instead of Config\Tuning
#   ... -Out <dir>       output directory (default Saved\Scenarios\<name>)
# Prints a one-line verdict plus the checks and writes <Out>\metrics.json and <Out>\trace.json.
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
# Remove stale results so an old pass can never be mistaken for this run.
foreach ($stale in @('metrics.json', 'trace.json', 'final.png')) {
    $path = Join-Path $Out $stale
    if (Test-Path $path) { Remove-Item $path -Force }
}

$arguments = "-game -Scenario=`"$scenarioFile`" -Out=`"$Out`" -UseFixedTimeStep -FPS=$fps"
# A scenario may name a map; it goes first, as the URL the engine opens.
if ($spec.map) { $arguments = "$($spec.map) $arguments" }
if ($Shot) { $Rendered = $true }
if ($VR) {
    $arguments += ' -vr -windowed -ResX=1280 -ResY=720'
} elseif ($Rendered) {
    $arguments += ' -RenderOffscreen -windowed -ResX=1280 -ResY=720'
} else {
    $arguments += ' -nullrhi'
}
if ($CVars) { $arguments += " -dpcvars=`"$CVars`"" }
if ($Tune) { $arguments += " -Tune=`"$Tune`"" }
if ($TuningDir) { $arguments += " -TuningDir=`"$([System.IO.Path]::GetFullPath($TuningDir))`"" }
if ($Shot) { $arguments += " -Shot=`"$(Join-Path $Out 'final.png')`"" }

$run = Invoke-Editor -ArgumentString $arguments -LogFile $logFile -TimeoutSec $TimeoutSec -VR:$VR
$metrics = Read-JsonFile $metricsFile

if ($run.TimedOut) {
    Write-Output "SCENARIO ERROR: $Name timed out after $TimeoutSec s. Log: $logFile"
    exit 2
}
if (-not $metrics) {
    Write-Output "SCENARIO ERROR: $Name wrote no metrics.json (process exit code $($run.ExitCode)). Log: $logFile"
    Get-LogErrors $logFile | ForEach-Object { Write-Output "  $_" }
    exit 2
}
if (-not $metrics.ok) {
    Write-Output "SCENARIO ERROR: $Name could not run: $($metrics.error)"
    exit 2
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
