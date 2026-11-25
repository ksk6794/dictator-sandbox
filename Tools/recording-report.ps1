# What is in recordings of a person's hands: each recording is replayed as its scenario, and the replay is read
# for what the hands did and what the body made of it. Made for the takes of a session in the headset (M4 part 1,
# step 4), whose figures are what the hand's force cap, mass and shape are tuned on in part 2.
#   powershell -ExecutionPolicy Bypass -File Tools\recording-report.ps1 -Name vr_check
#   ... -Name "takes*"       every recording under Saved\Recordings whose name matches (takes, takes_2, ...)
#   ... -Tune "effectors.hand.max_force_n=450"    replay with other values of the hand or the body
#   ... -Out <dir>           default Saved\Reports\<Name without wildcards>
# For each recording it prints: its length, how fast each hand's target went and how often its grip closed; what
# each hand delivered and to which parts of the body; every hit the body told of, with the hand that made it, how
# fast that hand was moving as it arrived and the largest contact force; what the body did (states, falls,
# get-ups); and whether the replay ends as the session did. Writes <Out>\report.json.
# A replay is a run of its own: a take replays within limits, not to the digit (docs/testing.md).
# Exit code: 0 reported, 2 a recording could not be replayed.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [string]$Tune,
    [string]$Out
)
. "$PSScriptRoot\common.ps1"

$fps = 90
$dt = 1.0 / $fps
$inv = [System.Globalization.CultureInfo]::InvariantCulture
$recordingsRoot = Join-Path $RepoRoot 'Saved\Recordings'
$dirs = @(Get-ChildItem $recordingsRoot -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -like $Name } | Sort-Object Name)
if ($dirs.Count -eq 0) {
    Write-Output "RECORDING REPORT ERROR: no recording under $recordingsRoot matches '$Name'"
    exit 2
}
if (-not $Out) { $Out = Join-Path $RepoRoot ("Saved\Reports\" + ($Name -replace '[^A-Za-z0-9_\-]', '')) }
$Out = [System.IO.Path]::GetFullPath($Out)
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
New-Item -ItemType Directory -Force $Out | Out-Null

$jobs = @()
$items = @()
foreach ($dir in $dirs) {
    $scenarioFile = Join-Path $dir.FullName "$($dir.Name).json"
    $recordingFile = Join-Path $dir.FullName "$($dir.Name).recording.json"
    if (-not (Test-Path $scenarioFile) -or -not (Test-Path $recordingFile)) { continue }
    $job = @{ File = $scenarioFile; Out = (Join-Path $Out $dir.Name); Tune = $Tune }
    $jobs += $job
    $items += @{ Name = $dir.Name; Dir = $dir.FullName; RunDir = $job.Out; Recording = $recordingFile }
}
if ($jobs.Count -eq 0) {
    Write-Output "RECORDING REPORT ERROR: the directories that match '$Name' hold no recording with its scenario"
    exit 2
}
Invoke-ScenarioBatch -Jobs $jobs -Parallel 8

function Read-Rows([string]$File) {
    if (-not (Test-Path $File)) { return $null }
    $lines = [System.IO.File]::ReadAllLines($File)
    $names = $lines[0].Split(',')
    $rows = New-Object System.Collections.ArrayList
    for ($i = 1; $i -lt $lines.Length; $i++) {
        if (-not $lines[$i]) { continue }
        $cells = $lines[$i].Split(',')
        $row = @{}
        for ($c = 0; $c -lt $names.Length; $c++) { $row[$names[$c]] = [double]::Parse($cells[$c], $inv) }
        [void]$rows.Add($row)
    }
    return $rows
}

$failed = $false
$report = @()
$lines = @()
$totalHits = 0
foreach ($item in $items) {
    $metricsOfRun = Read-JsonFile (Join-Path $item.RunDir 'metrics.json')
    $rows = Read-Rows (Join-Path $item.RunDir 'hands.csv')
    if (-not $metricsOfRun -or -not $metricsOfRun.ok -or -not $rows) {
        $lines += "  $($item.Name): the replay gave no result"
        $failed = $true
        continue
    }
    $recorded = Read-JsonFile $item.Recording
    $frames = $recorded.frames.Count
    $entry = [ordered]@{ name = $item.Name; frames = $frames; seconds = $frames * $dt; hands = [ordered]@{}; hits = @(); body = $null; replay = $null }
    $lines += ("  {0}: {1:N1} s" -f $item.Name, ($frames * $dt))

    # The hands' targets, from the recording itself.
    $handNames = @($recorded.effectors)
    for ($h = 0; $h -lt $handNames.Count; $h++) {
        $raw = New-Object 'double[]' $frames
        for ($i = 1; $i -lt $frames; $i++) {
            $a = $recorded.frames[$i][$h]; $b = $recorded.frames[$i - 1][$h]
            $raw[$i] = [Math]::Sqrt([Math]::Pow([double]$a[0] - [double]$b[0], 2) + [Math]::Pow([double]$a[1] - [double]$b[1], 2) + [Math]::Pow([double]$a[2] - [double]$b[2], 2)) / $dt / 100.0
        }
        $speeds = New-Object System.Collections.ArrayList
        $closed = 0; $closures = 0; $wasClosed = $false; $swings = 0; $inSwing = $false
        for ($i = 1; $i -lt $frames; $i++) {
            $a = $recorded.frames[$i][$h]
            # The middle of three frames' speeds: a swing lasts longer than a frame, a jump of the tracking does not.
            $three = @($raw[[Math]::Max($i - 1, 1)], $raw[$i], $raw[[Math]::Min($i + 1, $frames - 1)]) | Sort-Object
            $speed = $three[1]
            [void]$speeds.Add($speed)
            if ($speed -gt 2.0) { if (-not $inSwing) { $swings++; $inSwing = $true } } elseif ($speed -lt 1.0) { $inSwing = $false }
            $isClosed = ([double]$a[10]) -gt 0.5
            if ($isClosed) { $closed++ }
            if ($isClosed -and -not $wasClosed) { $closures++ }
            $wasClosed = $isClosed
        }
        $sorted = @($speeds | Sort-Object)
        $p95 = 0.0; $peak = 0.0
        if ($sorted.Count -gt 0) { $p95 = $sorted[[int][Math]::Floor(($sorted.Count - 1) * 0.95)]; $peak = $sorted[-1] }
        $fromReplay = $metricsOfRun.effectors.list.($handNames[$h])
        $touched = @()
        if ($fromReplay -and $fromReplay.touched_impulse_ns) {
            $touched = @($fromReplay.touched_impulse_ns.PSObject.Properties | Sort-Object Value -Descending | Select-Object -First 4 | ForEach-Object { '{0} {1:N0}' -f $_.Name, $_.Value })
        }
        $impulse = 0.0; $peakForce = 0.0
        if ($fromReplay) { $impulse = [double]$fromReplay.contact_impulse_ns; $peakForce = [double]$fromReplay.max_contact_force_n }
        $lines += ("    {0}: target up to {1:N1} m/s (95th percentile {2:N1}), {3} swings over 2 m/s, grip closed {4} times for {5:N0}% of the time; delivered {6:N0} N*s, at most {7:N0} N in a frame ({8})" -f `
            $handNames[$h], $peak, $p95, $swings, $closures, (100.0 * $closed / [Math]::Max($frames, 1)), $impulse, $peakForce, ($touched -join ', '))
        $entry.hands[$handNames[$h]] = [ordered]@{ peak_target_speed_mps = $peak; p95_target_speed_mps = $p95; swings = $swings; grip_closures = $closures; grip_closed_share = $closed / [Math]::Max($frames, 1); contact_impulse_ns = $impulse; max_contact_force_n = $peakForce; touched = $touched }
    }

    # What the runner saw of the hands from outside, and what bodies they were.
    $outside = $metricsOfRun.effectors
    $changes = 0; $grips = 0
    foreach ($hand in $handNames) {
        $one = $outside.list.$hand
        if ($one) { $changes += [int]$one.shape_changes; $grips += [int]$one.grips_on_body }
    }
    $lines += ("    the hands: {0} frame(s) of tunnelling, at most {1:N2} cm inside a body; {2} change(s) between fist and open hand as bodies, {3} grip(s) closed on the body" -f [int]$outside.tunnel_frames, [double]$outside.max_penetration_cm, $changes, $grips)
    $entry['hands_outside'] = [ordered]@{ tunnel_frames = [int]$outside.tunnel_frames; max_penetration_cm = [double]$outside.max_penetration_cm; shape_changes = $changes; grips_on_body = $grips }

    # What the hands took hold of (effectors.hand.grab), a line a hand that held anything.
    $held = @()
    foreach ($hand in $handNames) {
        $one = $outside.list.$hand
        if (-not $one -or [int]$one.grabs -lt 1) { continue }
        $meanText = 'no'
        if ($null -ne $one.mean_grab_force_n) { $meanText = ('{0:N0}' -f [double]$one.mean_grab_force_n) }
        $lines += ("    {0} took hold {1} time(s), last of {2}: held for {3:N1} s in all, pulled with {4} N on average and {5:N0} N at most; the grip slipped {6} time(s), was torn open {7}, lost what it held {8}" -f `
            $hand, [int]$one.grabs, $one.last_grabbed, ([double]$one.grab_frames / $fps), $meanText, [double]$one.max_grab_force_n,
            [int]$one.grab_slips, [int]$one.grab_overloads, [int]$one.grabs_lost)
        $held += [ordered]@{ hand = $hand; grabs = [int]$one.grabs; last_grabbed = $one.last_grabbed; held_s = [Math]::Round([double]$one.grab_frames / $fps, 2)
            mean_force_n = $one.mean_grab_force_n; max_force_n = $one.max_grab_force_n; slips = [int]$one.grab_slips; torn = [int]$one.grab_overloads; lost = [int]$one.grabs_lost }
    }
    $entry['grabs'] = $held

    # Every hit the body told of, with the hand that made it.
    $events = @($metricsOfRun.control.events)
    $hits = @($events | Where-Object { $_.type -eq 'hit' })
    $totalHits += $hits.Count
    foreach ($hit in $hits) {
        $at = [int][Math]::Round([double]$hit.t * $fps) + 1
        $best = $null; $bestForce = 0.0
        foreach ($hand in $handNames) {
            $force = 0.0
            for ($i = [Math]::Max($at - 3, 0); $i -lt [Math]::Min($at + 12, $rows.Count); $i++) { $force = [Math]::Max($force, $rows[$i]["${hand}_contact_n"]) }
            if ($force -gt $bestForce) { $bestForce = $force; $best = $hand }
        }
        $arrived = 0.0
        $delivered = 0.0
        if ($best) {
            for ($i = [Math]::Max($at - 8, 0); $i -lt [Math]::Min($at + 1, $rows.Count); $i++) { $arrived = [Math]::Max($arrived, $rows[$i]["${best}_speed_mps"]) }
            # What the hand delivered in this contact: its contact force summed from the hit until the hand has
            # touched nothing for three frames.
            $quiet = 0
            for ($i = [Math]::Max($at - 3, 0); $i -lt $rows.Count -and $quiet -lt 3; $i++) {
                $force = $rows[$i]["${best}_contact_n"]
                if ($force -gt 0.0) { $delivered += $force * $dt; $quiet = 0 } elseif ($delivered -gt 0.0) { $quiet++ }
            }
        }
        $by = 'no hand'
        if ($best) { $by = $best }
        $lines += ("      hit at {0,6:N2} s on {1,-12} by {2} arriving at {3:N1} m/s: {4,5:N1} N*s in that contact, up to {5:N0} N; the body was {6}" -f [double]$hit.t, $hit.bone, $by, $arrived, $delivered, $bestForce, $hit.state)
        $entry.hits += [ordered]@{ t = [double]$hit.t; bone = $hit.bone; hand = $best; arrival_speed_mps = $arrived; contact_impulse_ns = $delivered; max_contact_force_n = $bestForce; state = $hit.state }
    }

    # What the body did.
    $states = @($events | Where-Object { $_.type -eq 'state' } | ForEach-Object { $_.state })
    $count = @{}
    foreach ($state in $states) { if ($count.ContainsKey($state)) { $count[$state]++ } else { $count[$state] = 1 } }
    $summary = @($count.Keys | Sort-Object | ForEach-Object { "$_ $($count[$_])" }) -join ', '
    if (-not $summary) { $summary = 'stood throughout' }
    $endState = 'standing'
    if ($states.Count -gt 0) { $endState = $states[-1] }
    $lines += ("    the body: {0} hits; {1}; ends {2}" -f $hits.Count, $summary, $endState)
    $entry.body = [ordered]@{ hit_events = $hits.Count; states = $count; end_state = $endState }

    # Whether the replay ends as the session did.
    $seen = Read-JsonFile (Join-Path $item.Dir "$($item.Name).session.json")
    if ($seen -and $seen.state_sequence -and $seen.pelvis_cm) {
        $replayed = @('standing') + $states
        $sameStates = (($replayed -join '>') -eq $seen.state_sequence)
        $pelvis = @($metricsOfRun.body.bodies | Where-Object { $_.bone -eq 'pelvis' })[0]
        $apart = -1.0
        if ($pelvis) {
            $apart = [Math]::Sqrt([Math]::Pow([double]$pelvis.final_location_cm[0] - [double]$seen.pelvis_cm[0], 2) + [Math]::Pow([double]$pelvis.final_location_cm[1] - [double]$seen.pelvis_cm[1], 2) + [Math]::Pow([double]$pelvis.final_location_cm[2] - [double]$seen.pelvis_cm[2], 2))
        }
        $verdict = 'as the session saw it'
        if (-not $sameStates) { $verdict = 'NOT as the session saw it: the states differ' }
        $lines += ("    the replay: {0}; the pelvis ends {1:N2} cm from where the session had it" -f $verdict, $apart)
        $entry.replay = [ordered]@{ same_states = $sameStates; pelvis_apart_cm = $apart }
    }
    $report += $entry
}

$verdictWord = 'OK'
if ($failed) { $verdictWord = 'INCOMPLETE' }
Write-Output ("RECORDING REPORT {0}: {1} recording(s) matching '{2}', {3} hits on the body in all" -f $verdictWord, $items.Count, $Name, $totalHits)
$lines | ForEach-Object { Write-Output $_ }
Write-JsonFile (Join-Path $Out 'report.json') ([ordered]@{ name = $Name; tune = $Tune; recordings = $report })
Write-Output "  result: $(Join-Path $Out 'report.json')"
if ($failed) { exit 2 }
exit 0
