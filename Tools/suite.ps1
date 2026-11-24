# Runs every scenario in Scenarios\ headless, several at a time, and prints one line per scenario.
#   powershell -ExecutionPolicy Bypass -File Tools\suite.ps1 [-Filter drop*]
#   ... -Parallel 8            scenario processes at a time. 1 runs them one after another, which is the way
#                              to get physics step times that nothing else disturbed
#   ... -SaveBaseline <name>   afterwards keep the results as Saved\Baselines\<name> (Tools\baseline.ps1 -Save)
#   ... -Baseline <name>       afterwards say which scenarios differ from that baseline (Tools\baseline.ps1 -Compare)
#   ... -Long                  also the scenarios that simulate more than a minute (stand_npc_10min: drift over ten
#                              minutes of standing). Left out by default: one of them took 60 of the suite's 80 s.
#                              Run them at the end of a part and at a milestone's gate
#   ... -Tune "name=value,..." tuning overrides for every run: the whole suite against other values. Its runs
#                              and its result file then go to Saved\Suites\<Label> (-Label, "tuned" by default)
#                              and leave Saved\Scenarios and Saved\Tools\suite.json, which baselines are made
#                              of, as they were; -SaveBaseline and -Baseline are refused with it
# Besides each scenario's own expectations it holds the whole set to the limits that are stated for "any
# scenario": no simulated body's joints stretch by more than -MaxJointStretchCm, and no scenario's physics
# step takes more than -MaxPhysicsStepMeanMs on average. The worst scenario of each is printed.
#
# The physics step is timed by the clock, and runs that share the processor read longer than they are. So a
# scenario that is over a step limit after the parallel pass (its own max_physics_step_mean_ms or the limit
# for the set) is run once more by itself, and that run is the one that counts. Sharing the processor never
# makes a step read shorter, so nothing over the limit can slip through.
# Writes Saved\Tools\suite.json. Exit code: 0 all pass, 1 at least one failed, broke a limit or could not run.
param(
    [string]$Filter = '*',
    [int]$Parallel = 8,
    [double]$MaxJointStretchCm = 2.0,
    [double]$MaxPhysicsStepMeanMs = 1.5,
    [string]$SaveBaseline,
    [string]$Baseline,
    [switch]$Long,
    [string]$Tune,
    [string]$Label = 'tuned'
)
. "$PSScriptRoot\common.ps1"

if ($Tune -and ($SaveBaseline -or $Baseline)) {
    Write-Output 'SUITE ERROR: a suite run with -Tune is not what a baseline is made of or compared with'
    exit 1
}
$runRoot = Join-Path $RepoRoot 'Saved\Scenarios'
$resultDir = $ToolsOut
if ($Tune) {
    $runRoot = Join-Path $RepoRoot "Saved\Suites\$Label"
    if (Test-Path $runRoot) { Remove-Item $runRoot -Recurse -Force }
    New-Item -ItemType Directory -Force $runRoot | Out-Null
    $resultDir = $runRoot
}

$files = @(Get-ChildItem (Join-Path $RepoRoot 'Scenarios') -Filter '*.json' | Where-Object { $_.BaseName -like $Filter } | Sort-Object Name)
if (-not $Long) {
    $longFiles = @($files | Where-Object { [double](Read-JsonFile $_.FullName).duration_s -gt 60 })
    foreach ($file in $longFiles) { Write-Output ("SCENARIO SKIPPED: {0} ({1} s simulated; -Long runs it)" -f $file.BaseName, (Read-JsonFile $file.FullName).duration_s) }
    $files = @($files | Where-Object { $longFiles.FullName -notcontains $_.FullName })
}
if ($files.Count -eq 0) {
    Write-Output "SUITE FAILED: no scenario matches '$Filter'"
    exit 1
}

$started = Get-Date
$jobs = @()
foreach ($file in $files) {
    $job = @{ Name = $file.BaseName; Out = (Join-Path $runRoot $file.BaseName) }
    if ($Tune) { $job.Tune = $Tune }
    $jobs += $job
}
Invoke-ScenarioBatch -Jobs $jobs -Parallel $Parallel

function Read-SuiteResult($job) {
    $metrics = Read-JsonFile (Join-Path $job.Out 'metrics.json')
    $failedChecks = @()
    if ($metrics -and $metrics.checks) { $failedChecks = @($metrics.checks | Where-Object { -not $_.pass } | ForEach-Object { $_.name }) }
    # What the limits for the whole set are judged by. Stretch is null for a body that is not simulated.
    $stretch = $null
    $stepMs = $null
    if ($metrics -and $metrics.metrics -and $null -ne $metrics.metrics.max_joint_stretch_cm) { $stretch = [double]$metrics.metrics.max_joint_stretch_cm }
    if ($metrics -and $metrics.physics_step_ms) { $stepMs = [double]$metrics.physics_step_ms.mean }
    $bodies = $null
    $bones = $null
    if ($metrics -and $metrics.hashes) { $bodies = $metrics.hashes.bodies; $bones = $metrics.hashes.bones }
    return [ordered]@{
        scenario = $job.Name; pass = [bool]($metrics -and $metrics.ok -and $metrics.pass); failed_checks = $failedChecks
        max_joint_stretch_cm = $stretch; physics_step_mean_ms = $stepMs; timed_alone = ($Parallel -le 1)
        hash_bodies = $bodies; hash_bones = $bones
        verdict = (Format-ScenarioVerdict $job.Name $metrics $job.Out)
    }
}

$results = @()
foreach ($job in $jobs) {
    $result = Read-SuiteResult $job
    # Over a step limit while sharing the processor, and nothing else wrong: measured again, alone.
    $overOwnLimit = ($result.failed_checks.Count -gt 0) -and (@($result.failed_checks | Where-Object { $_ -ne 'max_physics_step_mean_ms' }).Count -eq 0)
    $overSetLimit = ($null -ne $result.physics_step_mean_ms) -and ($result.physics_step_mean_ms -gt $MaxPhysicsStepMeanMs)
    if ($Parallel -gt 1 -and ($overOwnLimit -or $overSetLimit)) {
        $shared = $result.physics_step_mean_ms
        Invoke-ScenarioBatch -Jobs @($job) -Parallel 1
        $result = Read-SuiteResult $job
        $result.timed_alone = $true
        $result.verdict += (" [physics step {0:N3} ms alone, {1:N3} ms while sharing the processor]" -f $result.physics_step_mean_ms, $shared)
    }
    Write-Output $result.verdict
    $results += $result
}
$seconds = [math]::Round(((Get-Date) - $started).TotalSeconds, 1)

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
Write-JsonFile (Join-Path $resultDir 'suite.json') ([ordered]@{
    ok       = $ok
    total    = $results.Count
    passed   = $results.Count - $failed.Count
    parallel = $Parallel
    seconds  = $seconds
    limits   = $limits
    results  = $results
})

if ($ok) {
    Write-Output "SUITE OK: $($results.Count) scenario(s) passed in $seconds s, $Parallel at a time"
} else {
    Write-Output "SUITE FAILED: $($failed.Count) of $($results.Count) scenario(s) did not pass, $($brokenLimits.Count) limit(s) of the whole set broken"
    foreach ($entry in $failed) {
        Write-Output "  $($entry.scenario): $($entry.failed_checks -join ', ')"
    }
}
foreach ($limit in $limits) {
    $mark = 'ok  '
    if (-not $limit.pass) { $mark = 'FAIL' }
    $note = ''
    if ($limit.name -eq 'max_physics_step_mean_ms' -and $Parallel -gt 1) { $note = '; timed while sharing the processor, -Parallel 1 for undisturbed figures' }
    Write-Output ("  [{0}] {1}: {2} {3} in {4} (limit {5} {3}, {6} scenario(s) measured{7})" -f $mark, $limit.what, $limit.worst, $limit.unit, $limit.worst_scenario, $limit.limit, $limit.measured_in, $note)
    if (-not $limit.pass) { Write-Output "         over the limit: $($limit.over -join ', ')" }
}

$baselineScript = Join-Path $PSScriptRoot 'baseline.ps1'
if ($SaveBaseline) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $baselineScript -Save $SaveBaseline | ForEach-Object { Write-Output $_ }
}
if ($Baseline) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $baselineScript -Compare $Baseline | ForEach-Object { Write-Output $_ }
}
if ($ok) { exit 0 }
exit 1
