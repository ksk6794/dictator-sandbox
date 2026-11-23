# Keeps the results of a suite run as a baseline, or says how the latest suite run differs from one.
#   powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -Save m3_part2_start
#   powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -Compare m3_part2_start [-FailOnChange]
#   powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -CompareMatrix m3_part2_start   (the latest push matrix against the baseline's)
#   powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -List
# A baseline is a copy of Saved\Tools\suite.json, of the metrics.json of every scenario in it and, if there
# is one, of the push matrix's result, under Saved\Baselines\<name>. It is taken before a change and compared
# after it: a scenario the change should not touch must come out with the same two hashes.
#
# -Compare reads the latest suite run (Saved\Tools\suite.json and Saved\Scenarios) and prints, per scenario
# whose hashes differ, what else changed: verdict, states, drift, stretch, assist load and so on. Hashes are
# compared within one machine and build chain only; they are not committed (docs/testing.md, policy 3).
# Writes Saved\Tools\baseline-compare.json. Exit code: 0, or with -FailOnChange 1 if any scenario of the
# baseline differs or is missing; 2 if there is nothing to compare.
param(
    [string]$Save,
    [string]$Compare,
    [string]$CompareMatrix,
    [switch]$List,
    [switch]$FailOnChange
)
. "$PSScriptRoot\common.ps1"

$baselineRoot = Join-Path $RepoRoot 'Saved\Baselines'
$suiteFile = Join-Path $ToolsOut 'suite.json'
$matrixFile = Join-Path $RepoRoot 'Saved\Matrix\push\matrix.json'

if ($List) {
    if (Test-Path $baselineRoot) {
        Get-ChildItem $baselineRoot -Directory | Sort-Object LastWriteTime | ForEach-Object {
            Write-Output ("{0}  ({1:yyyy-MM-dd HH:mm})" -f $_.Name, $_.LastWriteTime)
        }
    }
    exit 0
}

if ($Save) {
    $suite = Read-JsonFile $suiteFile
    if (-not $suite) { Write-Output "BASELINE ERROR: no suite run to keep ($suiteFile); run Tools\suite.ps1 first"; exit 2 }
    $destination = Join-Path $baselineRoot $Save
    if (Test-Path $destination) { Remove-Item $destination -Recurse -Force }
    New-Item -ItemType Directory -Force $destination | Out-Null
    Copy-Item $suiteFile (Join-Path $destination 'suite.json')
    $kept = 0
    foreach ($result in $suite.results) {
        $metrics = Join-Path $RepoRoot "Saved\Scenarios\$($result.scenario)\metrics.json"
        if (Test-Path $metrics) {
            New-Item -ItemType Directory -Force (Join-Path $destination "scenarios\$($result.scenario)") | Out-Null
            Copy-Item $metrics (Join-Path $destination "scenarios\$($result.scenario)\metrics.json")
            $kept++
        }
    }
    if (Test-Path $matrixFile) { Copy-Item $matrixFile (Join-Path $destination 'push-matrix.json') }
    Write-Output "BASELINE SAVED: $Save, $kept scenario(s), in $destination"
    exit 0
}

if ($CompareMatrix) {
    # The latest push matrix against the baseline's, run by run: the outcome (a baseline from before the
    # outcome classes has only "fell"), the steps and the state sequence.
    $oldMatrix = Read-JsonFile (Join-Path (Join-Path $baselineRoot $CompareMatrix) 'push-matrix.json')
    $newMatrix = Read-JsonFile $matrixFile
    if (-not $oldMatrix) { Write-Output "BASELINE ERROR: baseline '$CompareMatrix' has no push matrix"; exit 2 }
    if (-not $newMatrix) { Write-Output "BASELINE ERROR: no push matrix to compare ($matrixFile); run Tools\push-matrix.ps1 first"; exit 2 }
    function Get-Outcome($run) {
        if ($null -ne $run.outcome) { return $run.outcome }
        if ($run.fell) { return 'fell' }
        return 'stood'
    }
    $oldByName = @{}
    foreach ($run in $oldMatrix.results) { $oldByName[$run.name] = $run }
    $sameRuns = 0
    $changedRuns = @()
    $newRuns = 0
    foreach ($run in $newMatrix.results) {
        $old = $oldByName[$run.name]
        if (-not $old) { $newRuns++; continue }
        $was = "$(Get-Outcome $old) [$($old.state_sequence)]"
        $is = "$(Get-Outcome $run) [$($run.state_sequence)]"
        if ($was -eq $is) { $sameRuns++ } else { $changedRuns += [ordered]@{ name = $run.name; before = $was; after = $is } }
    }
    Write-JsonFile (Join-Path $ToolsOut 'baseline-matrix-compare.json') ([ordered]@{ baseline = $CompareMatrix; same = $sameRuns; changed = $changedRuns; new = $newRuns })
    Write-Output ("BASELINE MATRIX {0}: {1} run(s) end the same way, {2} differently, {3} new" -f $CompareMatrix, $sameRuns, $changedRuns.Count, $newRuns)
    foreach ($entry in $changedRuns) { Write-Output "  $($entry.name): $($entry.before) -> $($entry.after)" }
    exit 0
}

if (-not $Compare) {
    Write-Output 'BASELINE ERROR: give -Save <name>, -Compare <name>, -CompareMatrix <name> or -List'
    exit 2
}

$source = Join-Path $baselineRoot $Compare
$before = Read-JsonFile (Join-Path $source 'suite.json')
$after = Read-JsonFile $suiteFile
if (-not $before) { Write-Output "BASELINE ERROR: there is no baseline '$Compare' in $baselineRoot"; exit 2 }
if (-not $after) { Write-Output "BASELINE ERROR: no suite run to compare ($suiteFile); run Tools\suite.ps1 first"; exit 2 }

# What is shown for a scenario whose hashes changed: a name, where it is in metrics.json, digits to print.
$fields = @(
    @{ name = 'pass'; path = 'pass' },
    @{ name = 'states'; path = 'control.state_sequence' },
    @{ name = 'fell'; path = 'metrics.fell' },
    @{ name = 'pelvis drift cm'; path = 'metrics.max_pelvis_drift_cm'; digits = 2 },
    @{ name = 'joint stretch cm'; path = 'metrics.max_joint_stretch_cm'; digits = 3 },
    @{ name = 'fastest joint rad/s'; path = 'metrics.max_joint_speed_rads'; digits = 1 },
    @{ name = 'limit violation deg'; path = 'metrics.max_joint_limit_violation_deg'; digits = 1 },
    @{ name = 'settled angular rad/s'; path = 'metrics.max_settled_angular_speed_rads'; digits = 3 },
    @{ name = 'assist force share'; path = 'control.assist_load.force_share_mean'; digits = 4 },
    @{ name = 'capture margin min cm'; path = 'control.balance.capture_margin_min_cm'; digits = 1 },
    @{ name = 'rest after fall s'; path = 'control.fall.rest_after_s'; digits = 2 },
    @{ name = 'energy gain J'; path = 'energy.max_gain_j'; digits = 1 },
    @{ name = 'penetration cm'; path = 'penetration.max_world_cm'; digits = 2 },
    @{ name = 'hand impulse N*s'; path = 'effectors.contact_impulse_ns'; digits = 2 }
)

function Format-Field($value, $digits) {
    if ($null -eq $value) { return '-' }
    if ($value -is [bool] -or $value -is [string]) { return "$value" }
    if ($null -ne $digits) { return ([math]::Round([double]$value, $digits)).ToString([System.Globalization.CultureInfo]::InvariantCulture) }
    return "$value"
}

$beforeByName = @{}
foreach ($result in $before.results) { $beforeByName[$result.scenario] = $result }
$afterNames = @($after.results | ForEach-Object { $_.scenario })

$same = @()
$changed = @()
$added = @()
foreach ($result in $after.results) {
    $name = $result.scenario
    if (-not $beforeByName.ContainsKey($name)) { $added += $name; continue }
    $old = Read-JsonFile (Join-Path $source "scenarios\$name\metrics.json")
    $new = Read-JsonFile (Join-Path $RepoRoot "Saved\Scenarios\$name\metrics.json")
    $oldHashes = "$(Get-JsonPath $old 'hashes.bodies') $(Get-JsonPath $old 'hashes.bones')"
    $newHashes = "$(Get-JsonPath $new 'hashes.bodies') $(Get-JsonPath $new 'hashes.bones')"
    if ($old -and $new -and $oldHashes -eq $newHashes) { $same += $name; continue }
    $differences = @()
    foreach ($field in $fields) {
        $was = Format-Field (Get-JsonPath $old $field.path) $field.digits
        $is = Format-Field (Get-JsonPath $new $field.path) $field.digits
        if ($was -ne $is) { $differences += [ordered]@{ field = $field.name; before = $was; after = $is } }
    }
    $changed += [ordered]@{ scenario = $name; hashes_before = $oldHashes; hashes_after = $newHashes; differences = $differences }
}
$missing = @($beforeByName.Keys | Where-Object { $afterNames -notcontains $_ } | Sort-Object)

Write-JsonFile (Join-Path $ToolsOut 'baseline-compare.json') ([ordered]@{
    baseline = $Compare; same = $same; changed = $changed; added = $added; missing = $missing
})

Write-Output ("BASELINE {0}: {1} scenario(s) with the same hashes, {2} changed, {3} new, {4} no longer run" -f $Compare, $same.Count, $changed.Count, $added.Count, $missing.Count)
foreach ($entry in $changed) {
    Write-Output "  changed: $($entry.scenario)  (bodies and bones $($entry.hashes_before) -> $($entry.hashes_after))"
    foreach ($difference in $entry.differences) {
        Write-Output ("      {0}: {1} -> {2}" -f $difference.field, $difference.before, $difference.after)
    }
    if ($entry.differences.Count -eq 0) { Write-Output '      none of the listed figures moved at the printed precision' }
}
if ($added.Count -gt 0) { Write-Output "  new: $($added -join ', ')" }
if ($missing.Count -gt 0) { Write-Output "  no longer run: $($missing -join ', ')" }

if ($FailOnChange -and ($changed.Count -gt 0 -or $missing.Count -gt 0)) { exit 1 }
exit 0
