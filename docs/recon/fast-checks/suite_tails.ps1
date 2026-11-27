# What an early end of scenarios could save (docs/fast-checks.md, SP-9): the suite's simulated time, the time after each
# scenario's last input, and, from the latest suite run (Saved\Scenarios), the time a window of standing or of rest would cut
# if every scenario took it. Reads files only.
#   powershell -ExecutionPolicy Bypass -File docs\recon\fast-checks\suite_tails.ps1 [-StandS 5] [-RestS 3]
param([double]$StandS = 5.0, [double]$RestS = 3.0)
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$rows = @()
foreach ($f in Get-ChildItem (Join-Path $root 'Scenarios') -Filter *.json) {
    $s = Get-Content $f.FullName -Raw | ConvertFrom-Json
    $d = [double]$s.duration_s
    if ($d -gt 60) { continue }
    # The last input: an event's end (t + duration_s), and for a hand or a foot the last key of its path.
    $last = 0.0; $lastRunner = 0.0
    foreach ($e in @($s.events)) {
        if (-not $e) { continue }
        $end = [double]$e.t
        if ($e.duration_s) { $end += [double]$e.duration_s }
        if ($end -gt $lastRunner) { $lastRunner = $end }
        if ($e.path) { $end = [double]$e.t + [double](@($e.path)[-1].t) }
        if ($end -gt $last) { $last = $end }
    }
    $m = $null
    $file = Join-Path $root "Saved\Scenarios\$($f.BaseName)\metrics.json"
    if (Test-Path $file) { $m = Get-Content $file -Raw | ConvertFrom-Json }
    $rows += [pscustomobject]@{ name = $f.BaseName; dur = $d; last = $last; lastRunner = $lastRunner; recording = [bool]$s.recording; metrics = $m }
}
'scenarios: {0}; simulated {1:N1} s; after the last input {2:N1} s (recordings left out: {3})' -f $rows.Count, (($rows | Measure-Object dur -Sum).Sum),
    (($rows | Where-Object { -not $_.recording } | ForEach-Object { [Math]::Max(0, $_.dur - $_.last) } | Measure-Object -Sum).Sum), @($rows | Where-Object recording).Count
"a hand's or a foot's path ends after the runner's last input (t + duration_s of the events) in $(@($rows | Where-Object { $_.last -gt $_.lastRunner + 0.01 }).Count) scenarios"
$stand = 0.0; $rest = 0.0; $standing = 0; $lying = 0
foreach ($x in $rows) {
    if (-not $x.metrics) { continue }
    $states = @($x.metrics.control.states)
    if ($x.metrics.control.end_state -eq 'standing' -and $states.Count -gt 0) {
        $standing++
        $endAt = [Math]::Max([double]$states[-1].t, $x.last) + $StandS
        if ($endAt -lt $x.dur) { $stand += $x.dur - $endAt }
    } elseif ($x.metrics.control.end_state -eq 'lying' -and $null -ne $x.metrics.control.fall.rest_after_s) {
        $lying++
        $endAt = [Math]::Max([double]$x.metrics.control.fall.started_t + [double]$x.metrics.control.fall.rest_after_s, $x.last) + $RestS
        if ($endAt -lt $x.dur) { $rest += $x.dur - $endAt }
    }
}
'if every scenario took the early end: {0} standing at the end, {1:N1} s cut by {2} s of standing; {3} lying, {4:N1} s cut by {5} s at rest' -f $standing, $stand, $StandS, $lying, $rest, $RestS
$getups = @($rows | Where-Object { $_.name -like 'getup*' })
'get-up scenarios (getup*): {0}, lasting {1} s' -f $getups.Count, ((($getups | ForEach-Object { $_.dur }) | Sort-Object -Unique) -join ', ')
