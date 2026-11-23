# Runs every scenario in Scenarios\ headless and prints one line per scenario.
#   powershell -ExecutionPolicy Bypass -File Tools\suite.ps1 [-Filter drop*]
# Besides each scenario's own expectations it holds the whole set to the limits that are stated for "any
# scenario": no simulated body's joints stretch by more than -MaxJointStretchCm, and no scenario's physics
# step takes more than -MaxPhysicsStepMeanMs on average. The worst scenario of each is printed.
# Writes Saved\Tools\suite.json. Exit code: 0 all pass, 1 at least one failed, broke a limit or could not run.
param(
    [string]$Filter = '*',
    [double]$MaxJointStretchCm = 2.0,
    [double]$MaxPhysicsStepMeanMs = 1.5
)
. "$PSScriptRoot\common.ps1"

$scenarioScript = Join-Path $PSScriptRoot 'scenario.ps1'
$files = @(Get-ChildItem (Join-Path $RepoRoot 'Scenarios') -Filter '*.json' | Where-Object { $_.BaseName -like $Filter } | Sort-Object Name)
if ($files.Count -eq 0) {
    Write-Output "SUITE FAILED: no scenario matches '$Filter'"
    exit 1
}

$results = @()
foreach ($file in $files) {
    $name = $file.BaseName
    & powershell -NoProfile -ExecutionPolicy Bypass -File $scenarioScript -Name $name -Quiet | ForEach-Object { Write-Output $_ }
    $code = $LASTEXITCODE
    $metrics = Read-JsonFile (Join-Path $RepoRoot "Saved\Scenarios\$name\metrics.json")
    $failedChecks = @()
    if ($metrics -and $metrics.checks) { $failedChecks = @($metrics.checks | Where-Object { -not $_.pass } | ForEach-Object { $_.name }) }
    # What the limits for the whole set are judged by. Stretch is null for a body that is not simulated.
    $stretch = $null
    $stepMs = $null
    if ($metrics -and $metrics.metrics -and $null -ne $metrics.metrics.max_joint_stretch_cm) { $stretch = [double]$metrics.metrics.max_joint_stretch_cm }
    if ($metrics -and $metrics.physics_step_ms) { $stepMs = [double]$metrics.physics_step_ms.mean }
    $results += [ordered]@{
        scenario = $name; pass = ($code -eq 0); exit_code = $code; failed_checks = $failedChecks
        max_joint_stretch_cm = $stretch; physics_step_mean_ms = $stepMs
    }
}

# The limits that hold for every scenario, whatever its own expectations say.
$limits = @()
foreach ($limit in @(
        @{ name = 'max_joint_stretch_cm'; field = 'max_joint_stretch_cm'; limit = $MaxJointStretchCm; unit = 'cm'; what = 'largest joint stretch of a simulated body' },
        @{ name = 'max_physics_step_mean_ms'; field = 'physics_step_mean_ms'; limit = $MaxPhysicsStepMeanMs; unit = 'ms'; what = 'longest mean physics step' })) {
    $measured = @($results | Where-Object { $null -ne $_[$limit.field] } | Sort-Object { $_[$limit.field] } -Descending)
    if ($measured.Count -eq 0) { continue }
    $worst = $measured[0]
    $over = @($measured | Where-Object { $_[$limit.field] -gt $limit.limit } | ForEach-Object { $_.scenario })
    $limits += [ordered]@{
        name = $limit.name; pass = ($over.Count -eq 0); limit = $limit.limit; worst = [math]::Round($worst[$limit.field], 4)
        worst_scenario = $worst.scenario; measured_in = $measured.Count; over = $over; what = $limit.what; unit = $limit.unit
    }
}

$failed = @($results | Where-Object { -not $_.pass })
$brokenLimits = @($limits | Where-Object { -not $_.pass })
$ok = ($failed.Count -eq 0) -and ($brokenLimits.Count -eq 0)
Write-JsonFile (Join-Path $ToolsOut 'suite.json') ([ordered]@{
    ok      = $ok
    total   = $results.Count
    passed  = $results.Count - $failed.Count
    limits  = $limits
    results = $results
})

if ($ok) {
    Write-Output "SUITE OK: $($results.Count) scenario(s) passed"
} else {
    Write-Output "SUITE FAILED: $($failed.Count) of $($results.Count) scenario(s) did not pass, $($brokenLimits.Count) limit(s) of the whole set broken"
    foreach ($entry in $failed) {
        Write-Output "  $($entry.scenario): $($entry.failed_checks -join ', ')"
    }
}
foreach ($limit in $limits) {
    $mark = 'ok  '
    if (-not $limit.pass) { $mark = 'FAIL' }
    Write-Output ("  [{0}] {1}: {2} {3} in {4} (limit {5} {3}, {6} scenario(s) measured)" -f $mark, $limit.what, $limit.worst, $limit.unit, $limit.worst_scenario, $limit.limit, $limit.measured_in)
    if (-not $limit.pass) { Write-Output "         over the limit: $($limit.over -join ', ')" }
}
if ($ok) { exit 0 }
exit 1
