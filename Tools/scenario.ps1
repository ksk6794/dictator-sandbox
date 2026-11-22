# Runs one scenario and reports its metrics.
#   powershell -ExecutionPolicy Bypass -File Tools\scenario.ps1 -Name drop_passive
#   ... -Rendered        render offscreen with the GPU instead of running without a renderer
#   ... -Shot            also save <Out>\final.png (implies -Rendered)
#   ... -Out <dir>       output directory (default Saved\Scenarios\<name>)
# Prints a one-line verdict plus the checks and writes <Out>\metrics.json and <Out>\trace.json.
# Exit code: 0 pass, 1 a check failed, 2 the scenario could not run.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [switch]$Rendered,
    [switch]$Shot,
    [string]$Out,
    [int]$TimeoutSec = 900,
    [switch]$Quiet
)
. "$PSScriptRoot\common.ps1"

$scenarioFile = Join-Path $RepoRoot "Scenarios\$Name.json"
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
if ($Shot) { $Rendered = $true }
if ($Rendered) {
    $arguments += ' -RenderOffscreen -windowed -ResX=1280 -ResY=720'
} else {
    $arguments += ' -nullrhi'
}
if ($Shot) { $arguments += " -Shot=`"$(Join-Path $Out 'final.png')`"" }

$run = Invoke-Editor -ArgumentString $arguments -LogFile $logFile -TimeoutSec $TimeoutSec
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
Write-Output ("SCENARIO {0}: {1} ({2}, {3} frames, {4:N1} s simulated in {5:N1} s, hashes bodies={6} bones={7})" -f `
    $verdict, $Name, $mode, $metrics.timing.frames, $metrics.timing.sim_time_s, $metrics.timing.wall_time_s, $metrics.hashes.bodies, $metrics.hashes.bones)
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
