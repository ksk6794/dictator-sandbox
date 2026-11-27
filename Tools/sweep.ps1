# Runs scenarios over a set of tuning values, several runs at a time, and gathers chosen metrics in one table.
#   powershell -ExecutionPolicy Bypass -File Tools\sweep.ps1 -Name legs `
#       -Scenarios "stand_npc,hit_head_30_stands" `
#       -Vary "muscles.legs.stiffness_nm_per_rad=400,800,1200;muscles.legs.damping_s=0.03,0.06,0.12"
#   ... -OneAtATime            change one value at a time and leave the others as the files have them (a
#                              coordinate search: 6 settings above instead of the grid's 9)
#   ... -Sets "|getup.start_blend_s=0.6|getup.start_blend_s=0,getup.carry=0"
#                              instead of -Vary: sets of values changed together, separated by '|', each a setting; an
#                              empty set is the files as they are. Several scenarios times several sets in one call, in
#                              place of a loop over Tools\scenario.ps1 (each run as that one would give it)
#   ... -Tune "name=value,..." overrides that every run gets, under the values being varied
#   ... -Files <dir>           also run every *.json scenario file of this directory (generated scenarios)
#   ... -Metrics "a.b,c.d"     the columns: paths into metrics.json (default: a general set, see below)
#   ... -Parallel 8            scenario processes at a time
#   ... -TuningDir <dir>       tuning files to start from instead of Config\Tuning
#
# Every setting of the varied values is run on every scenario with its values as overrides (-Tune), so the files
# in Config\Tuning are never written to; all the runs are jobs of one queue. Prints SWEEP DONE, then a verdict line a
# run as Tools\scenario.ps1 prints it (with the hashes), then one row per setting and scenario, and writes
# Saved\Sweeps\<Name>\sweep.json and sweep.csv; each run keeps its own metrics.json and series.csv under
# Saved\Sweeps\<Name>\runs\<setting>\<scenario>. Physics step times of a sweep are taken while the runs share the processor.
# Exit code: 0 the sweep ran (whatever the scenarios' verdicts), 2 a run produced no result or the arguments
# cannot be used.
param(
    [string]$Name = 'sweep',
    [string]$Scenarios,
    [string]$Files,
    [string]$Vary,
    [string]$Sets,
    [switch]$OneAtATime,
    [string]$Tune,
    [string]$TuningDir,
    [string]$Metrics = 'control.end_state,metrics.fell,metrics.max_pelvis_drift_cm,control.assist_load.force_share_mean,control.balance.capture_margin_min_cm,metrics.max_settled_angular_speed_rads,metrics.max_joint_speed_rads,metrics.max_joint_stretch_cm,energy.max_gain_j,control.fall.rest_after_s,penetration.max_world_cm',
    [int]$Parallel = 8
)
. "$PSScriptRoot\common.ps1"

$out = Join-Path $RepoRoot "Saved\Sweeps\$Name"
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force $out | Out-Null

# The scenarios: by name from Scenarios\, and every file of -Files.
$targets = @()
if ($Scenarios) {
    foreach ($scenario in $Scenarios.Split(',')) {
        $scenario = $scenario.Trim()
        if (-not $scenario) { continue }
        $file = Join-Path $RepoRoot "Scenarios\$scenario.json"
        if (-not (Test-Path $file)) { Write-Output "SWEEP ERROR: there is no scenario '$scenario'"; exit 2 }
        $targets += @{ name = $scenario; file = $file }
    }
}
if ($Files) {
    foreach ($file in @(Get-ChildItem $Files -Filter '*.json' | Sort-Object Name)) { $targets += @{ name = $file.BaseName; file = $file.FullName } }
}
if ($targets.Count -eq 0) { Write-Output 'SWEEP ERROR: give -Scenarios "a,b" or -Files <dir>'; exit 2 }

$withSets = $PSBoundParameters.ContainsKey('Sets')
if ($Vary -and $withSets) { Write-Output 'SWEEP ERROR: give -Vary or -Sets, not both'; exit 2 }
if (-not $Vary -and -not $withSets) { Write-Output 'SWEEP ERROR: give -Vary "name=v1,v2;..." or -Sets "set|set|..."'; exit 2 }
if ($withSets -and $OneAtATime) { Write-Output 'SWEEP ERROR: -OneAtATime is for -Vary'; exit 2 }

# The values: "name=v1,v2;name=v1,v2".
$axes = @()
foreach ($part in @("$Vary".Split(';'))) {
    $part = $part.Trim()
    if (-not $part) { continue }
    $pair = $part.Split('=')
    $values = @()
    if ($pair.Count -eq 2) { $values = @($pair[1].Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' }) }
    if ($pair.Count -ne 2 -or $values.Count -eq 0) { Write-Output "SWEEP ERROR: '$part' is not name=value,value,..."; exit 2 }
    $axes += @{ name = $pair[0].Trim(); values = $values }
}
if (-not $withSets -and $axes.Count -eq 0) { Write-Output 'SWEEP ERROR: -Vary names no value'; exit 2 }

# The settings: the sets as they are given, every combination of the axes, or one value changed at a time.
$settings = @()
if ($withSets) {
    foreach ($text in @("$Sets".Split('|'))) {
        $setting = @()
        foreach ($assignment in @($text.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
            $pair = $assignment.Split('=')
            if ($pair.Count -ne 2 -or -not $pair[0].Trim() -or -not $pair[1].Trim()) { Write-Output "SWEEP ERROR: '$assignment' in -Sets is not name=value"; exit 2 }
            $setting += @{ name = $pair[0].Trim(); value = $pair[1].Trim() }
        }
        $settings += , @($setting)
    }
}
elseif ($OneAtATime) {
    foreach ($axis in $axes) {
        foreach ($value in $axis.values) { $settings += , @(@{ name = $axis.name; value = $value }) }
    }
} else {
    $settings = @(, @())
    foreach ($axis in $axes) {
        $next = @()
        foreach ($setting in $settings) {
            foreach ($value in $axis.values) { $next += , (@($setting) + @(@{ name = $axis.name; value = $value })) }
        }
        $settings = $next
    }
}

$jobs = @()
$rows = @()
$index = 0
foreach ($setting in $settings) {
    $index++
    $assignments = @($setting | ForEach-Object { "$($_.name)=$($_.value)" }) -join ','
    # The overrides every run gets, then this setting's; an empty set adds nothing (the files as they are).
    $tuneArgument = (@($Tune, $assignments) | Where-Object { $_ }) -join ','
    $valuesText = $assignments
    if (-not $valuesText) { $valuesText = '(the files)' }
    foreach ($target in $targets) {
        $runOut = Join-Path $out ("runs\{0:000}\{1}" -f $index, $target.name)
        $job = @{ File = $target.file; Out = $runOut }
        if ($tuneArgument) { $job.Tune = $tuneArgument }
        if ($TuningDir) { $job.TuningDir = $TuningDir }
        $jobs += $job
        $rows += [ordered]@{ setting = $index; values = $valuesText; scenario = $target.name; out = $runOut }
    }
}

$started = Get-Date
Invoke-ScenarioBatch -Jobs $jobs -Parallel $Parallel
$seconds = [math]::Round(((Get-Date) - $started).TotalSeconds, 1)

$metricPaths = @($Metrics.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
$culture = [System.Globalization.CultureInfo]::InvariantCulture
function Format-Cell($value) {
    if ($null -eq $value) { return '' }
    if ($value -is [double] -or $value -is [single] -or $value -is [decimal]) { return ([math]::Round([double]$value, 4)).ToString($culture) }
    return "$value"
}

$missing = @()
$table = @()
foreach ($row in $rows) {
    # Not "$metrics": that is the -Metrics parameter, a string, and variable names do not tell case apart.
    $run = Read-JsonFile (Join-Path $row.out 'metrics.json')
    $entry = [ordered]@{ setting = $row.setting; values = $row.values; scenario = $row.scenario }
    if (-not $run -or -not $run.ok) {
        $missing += "$($row.values) on $($row.scenario)"
        $reason = 'no result'
        if ($run -and $run.error) { $reason = $run.error }
        $entry.pass = $false
        $entry.error = $reason
    } else {
        $entry.pass = [bool]$run.pass
        $entry.failed_checks = @($run.checks | Where-Object { -not $_.pass } | ForEach-Object { $_.name })
        foreach ($path in $metricPaths) { $entry[$path] = Get-JsonPath $run $path }
    }
    $table += $entry
}

Write-JsonFile (Join-Path $out 'sweep.json') ([ordered]@{
    name = $Name; seconds = $seconds; runs = $table.Count; vary = $Vary; sets = $(if ($withSets) { $Sets } else { $null }); one_at_a_time = [bool]$OneAtATime; tune = $Tune
    scenarios = @($targets | ForEach-Object { $_.name }); metrics = $metricPaths; rows = $table
})

# The table as text: for the console, and as CSV for anything else.
$header = @('setting', 'values', 'scenario', 'pass') + $metricPaths
$lines = @($header -join ',')
foreach ($entry in $table) {
    $cells = @($entry.setting, "`"$($entry.values)`"", $entry.scenario, $entry.pass)
    foreach ($path in $metricPaths) { $cells += (Format-Cell $entry[$path]) }
    $lines += ($cells -join ',')
}
[System.IO.File]::WriteAllLines((Join-Path $out 'sweep.csv'), $lines, (New-Object System.Text.UTF8Encoding $false))

Write-Output ("SWEEP DONE: {0}, {1} run(s) ({2} setting(s) on {3} scenario(s)) in {4} s" -f $Name, $table.Count, $settings.Count, $targets.Count, $seconds)
# A verdict line a run, as Tools\scenario.ps1 prints it, its setting after the scenario's name.
foreach ($row in $rows) {
    Write-Output (Format-ScenarioVerdict ('{0} [setting {1}]' -f $row.scenario, $row.setting) (Read-JsonFile (Join-Path $row.out 'metrics.json')) $row.out)
}
$shortNames = @($metricPaths | ForEach-Object { $_.Split('.')[-1] })
Write-Output ('  {0,-4} {1,-22} {2,-5} {3}   {4}' -f 'set', 'scenario', 'pass', (($shortNames | ForEach-Object { '{0,14}' -f ($_.Substring(0, [Math]::Min(14, $_.Length))) }) -join ' '), 'values')
foreach ($entry in $table) {
    $cells = @()
    foreach ($path in $metricPaths) {
        $text = Format-Cell $entry[$path]
        $cells += ('{0,14}' -f ($text.Substring(0, [Math]::Min(14, $text.Length))))
    }
    $scenarioText = $entry.scenario.Substring(0, [Math]::Min(22, $entry.scenario.Length))
    Write-Output ('  {0,-4} {1,-22} {2,-5} {3}   {4}' -f $entry.setting, $scenarioText, $entry.pass, ($cells -join ' '), $entry.values)
}
Write-Output "  table: $(Join-Path $out 'sweep.csv')"
if ($missing.Count -gt 0) {
    Write-Output "SWEEP ERROR: $($missing.Count) run(s) produced no result, first: $($missing[0]) ($($table | Where-Object { $_.error } | Select-Object -First 1 | ForEach-Object { $_.error }))"
    exit 2
}
exit 0
