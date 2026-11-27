# The benchmark of docs/fast-checks.md (SP-0): how long the checks and runs of section 3's table take, over three passes,
# the median of each kept; or, with -Hashes, the verdicts and both hashes of every run of every tool that runs scenarios,
# which a change of the queue, the start or the pool is held against (the rule of the hashes).
#   powershell -ExecutionPolicy Bypass -File Tools\bench.ps1 -Label before
#   ... -Passes 3                 passes over the table; the median of each measurement is kept
#   ... -Only "one_scenario,test" only these measurements (names below)
#   ... -Builds                   also the three builds of decision 8, each with the time of every action (build.ps1
#                                 -Timestamps): after the controller's .cpp and its header SandboxBodyController.h are
#                                 given a new time stamp (their content as it is: no script changes a source file), and a
#                                 full build (build.ps1 -Rebuild)
#   ... -Full                     also one bootstrap at the end. It writes Saved\Scenarios and Saved\Tools\suite.json, as it
#                                 always does: a bench with -Full belongs to a stage check, never between a step's
#                                 -SaveBaseline and its -Baseline
#   ... -Against <label>          the medians of that measurement printed beside this one's
#   ... -Hashes                   no times: every tool that runs scenarios runs once with its defaults, and the verdict, the
#                                 exit code and both hashes of every run it made are kept in Saved\Bench\<label>\hashes.json
#                                 (about 16 minutes). With -Against <label>: held against that one, run by run and tool by tool
#
# The measurements (section 3 of docs/fast-checks.md), each the wall time of the tool's call as the agent makes it:
#   one_scenario            Tools\scenario.ps1 -Name getup_left
#   sweep_4x3               Tools\sweep.ps1, getup_back, getup_front, getup_left and getup_right times getup.start_blend_s 0.3, 0.6, 0
#   test                    Tools\test.ps1
#   suite                   Tools\suite.ps1 (with -Label: into Saved\Suites\bench_<label>)
#   edit_build_tests_suite  the controller's .cpp given a new time stamp, then build.ps1, test.ps1 and suite.ps1 (each part kept)
#   getup_seeds             Tools\getup-seeds.ps1, three seeds
#   natural_quick           Tools\natural.ps1 -Quick
#   build_cpp, build_header, build_full    with -Builds
# Runs go under Saved\Bench\<label> or the tools' own folders with the label bench_<label>; never to Saved\Scenarios or
# Saved\Tools\suite.json, which baseline.ps1 -Compare reads (but with -Full). -Hashes puts three tools' runs where those
# tools always put them, Saved\Scenarios\<name> (a replay of a recording, the friction's drag, repeatability's runs under
# drop_passive\repeat), none of them a scenario of the suite.
#
# It measures on an idle machine: it refuses, and says why in its first line, while an engine process runs that it did not
# start (another session's run, the owner's editor) or less than 18 GB of memory is free, and stops if one appears.
# Progress goes to Saved\Bench\<label>\progress.log. Writes Saved\Bench\<label>.json (times) or Saved\Bench\<label>\hashes.json.
# Exit code: 0 measured (every tool gave its result, whatever its verdict), 1 with -Hashes -Against: a hash, a verdict or a
# run differs, 2 refused, stopped, or a tool gave no result.
param(
    [Parameter(Mandatory = $true)][string]$Label,
    [int]$Passes = 3,
    [string]$Only,
    [switch]$Builds,
    [switch]$Full,
    [string]$Against,
    [switch]$Hashes
)
. "$PSScriptRoot\common.ps1"

if ($Label -notmatch '^[A-Za-z0-9_\-\.]+$') { Write-Output "BENCH ERROR: -Label takes letters, digits, '_', '-' and '.', not '$Label'"; exit 2 }
if ($Passes -lt 1) { Write-Output 'BENCH ERROR: -Passes is 1 or more'; exit 2 }
$benchRoot = Join-Path $RepoRoot 'Saved\Bench'
$work = Join-Path $benchRoot $Label
$minFreeGB = 18.0

# --- The machine has to be idle -------------------------------------------------------------------------------------------

function Get-ForeignEngines {
    # Every engine process this bench's tools started has ended by the time it looks again: whatever runs then is someone else's.
    return @(Get-EngineProcesses | ForEach-Object { '{0} {1} (started {2:HH:mm:ss})' -f $_.Name, $_.Id, $_.StartTime })
}
$foreignAtStart = @(Get-ForeignEngines)
if ($foreignAtStart.Count -gt 0) {
    Write-Output "BENCH REFUSED: an engine process runs that this bench did not start ($($foreignAtStart -join ', ')): another session's run or the owner's editor. Measure on an idle machine"
    exit 2
}
$freeAtStart = Get-FreeMemoryGB
if ($freeAtStart -lt $minFreeGB) {
    Write-Output ("BENCH REFUSED: {0:N1} GB of memory free, {1:N0} GB needed (eight engine processes and the tests take 16.2 GB)" -f $freeAtStart, $minFreeGB)
    exit 2
}

$againstData = $null
if ($Against) {
    if ($Hashes) { $againstFile = Join-Path $benchRoot "$Against\hashes.json" } else { $againstFile = Join-Path $benchRoot "$Against.json" }
    $againstData = Read-JsonFile $againstFile
    if (-not $againstData) { Write-Output "BENCH ERROR: there is no measurement '$Against' ($againstFile)"; exit 2 }
}

# Each mode clears only what it makes: the hashes in Saved\Bench\<label> (hashes.json, hashes\, what the tools printed), the
# times in Saved\Bench\<label>\times and Saved\Bench\<label>.json.
if ($Hashes) {
    New-Item -ItemType Directory -Force $work | Out-Null
    foreach ($item in @(Get-ChildItem $work -ErrorAction SilentlyContinue | Where-Object { $_.Name -eq 'hashes.json' -or $_.Name -eq 'hashes' -or $_.Name -like 'printed_*' -or $_.Name -eq 'progress.log' })) {
        Remove-Item $item.FullName -Recurse -Force
    }
} else {
    $work = Join-Path $work 'times'
    if (Test-Path $work) { Remove-Item $work -Recurse -Force }
    New-Item -ItemType Directory -Force $work | Out-Null
    $timesFile = Join-Path $benchRoot "$Label.json"
    if (Test-Path $timesFile) { Remove-Item $timesFile -Force }
}
$progress = Join-Path $work 'progress.log'
function Add-BenchNote([string]$Text) {
    [System.IO.File]::AppendAllText($progress, ('{0:HH:mm:ss} {1}' -f (Get-Date), $Text) + [Environment]::NewLine)
}

$script:printedCount = 0
function Invoke-BenchTool {
    # One call of a tool as the agent makes it, timed; what it printed is kept. Throws when someone else's engine runs.
    param([string]$Script, [string[]]$Arguments, [hashtable]$Environment, [string]$Verdict)
    $foreign = @(Get-ForeignEngines)
    if ($foreign.Count -gt 0) { throw "an engine process this bench did not start began to run ($($foreign -join ', '))" }
    $script:printedCount++
    $printed = Join-Path $work ('printed_{0:D3}_{1}.txt' -f $script:printedCount, [System.IO.Path]::GetFileNameWithoutExtension($Script))
    $before = @{}
    if ($Environment) {
        foreach ($key in $Environment.Keys) { $before[$key] = [Environment]::GetEnvironmentVariable($key); [Environment]::SetEnvironmentVariable($key, $Environment[$key]) }
    }
    $started = Get-Date
    $lines = @()
    $code = $null
    try {
        if (-not $Arguments) { $Arguments = @() }
        $lines = @(& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot $Script) @Arguments)
        $code = $LASTEXITCODE
    } finally {
        foreach ($key in $before.Keys) { [Environment]::SetEnvironmentVariable($key, $before[$key]) }
    }
    $seconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2)
    [System.IO.File]::WriteAllLines($printed, [string[]]@($lines | ForEach-Object { [string]$_ }))
    $first = ''
    if ($lines.Count -gt 0) { $first = [string]$lines[0] }
    $verdictLine = Get-VerdictLine $lines $Verdict
    Add-BenchNote ('{0} {1}: {2:N1} s, exit {3}: {4}' -f $Script, ($Arguments -join ' '), $seconds, $code, $verdictLine)
    return [ordered]@{ script = $Script; arguments = ($Arguments -join ' '); seconds = $seconds; exit_code = $code; first_line = $first; verdict_line = $verdictLine; printed = $printed }
}

function Get-VerdictLine($Lines, [string]$Pattern) {
    # A tool's verdict: its first line, or the last line that matches its own pattern (the suite prints a line a scenario first).
    $all = @($Lines | ForEach-Object { [string]$_ })
    if ($Pattern) {
        $match = @($all | Where-Object { $_ -match $Pattern }) | Select-Object -Last 1
        if ($match) { return [string]$match }
    }
    # Not a note or warning of the queue that came before it.
    $first = @($all | Where-Object { $_ -notmatch '^QUEUE ' }) | Select-Object -First 1
    if ($first) { return [string]$first }
    return ''
}

function Resolve-BenchPath([string]$Path) {
    # A path of the repository's, or an absolute one as it is. (Join-Path would put the repository before an absolute one.)
    if ([System.IO.Path]::IsPathRooted($Path)) { return $Path }
    return (Join-Path $RepoRoot $Path)
}

function Set-SourceStamp([string]$Path) {
    # A new time stamp, the content as it is: what a build sees after an edit, without an edit.
    (Get-Item $Path).LastWriteTime = Get-Date
}

function Get-Median($Values) {
    $sorted = @($Values | Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ } | Sort-Object)
    if ($sorted.Count -eq 0) { return $null }
    $middle = [int][Math]::Floor(($sorted.Count - 1) / 2)
    if ($sorted.Count % 2 -eq 1) { return $sorted[$middle] }
    return ($sorted[$middle] + $sorted[$middle + 1]) / 2.0
}

$commit = ''
$dirty = 0
# git writes warnings (line endings) to its error stream, which Windows PowerShell under 'Stop' would turn into a terminating error.
$keepPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $commit = [string](& git -C $RepoRoot rev-parse --short HEAD 2>$null)
    $dirty = @(& git -C $RepoRoot status --porcelain 2>$null | Where-Object { $_ -match '^.. ' }).Count
} catch { }
$ErrorActionPreference = $keepPreference

# --- The hashes of every tool that runs scenarios -------------------------------------------------------------------------

if ($Hashes) {
    $h = Join-Path $work 'hashes'
    $tag = "bench_hashes_$Label"
    $getups = 'getup_back,getup_front,getup_left,getup_right'
    $battery = @(
        @{ name = 'suite'; script = 'suite.ps1'; arguments = @('-Label', $tag); runs = @("Saved\Suites\$tag"); verdict = '^SUITE ' },
        @{ name = 'push-matrix'; script = 'push-matrix.ps1'; arguments = @('-Out', "$h\matrix"); runs = @("$h\matrix\runs") },
        @{ name = 'knockdowns'; script = 'knockdowns.ps1'; arguments = @('-Out', "$h\knockdowns"); runs = @("$h\knockdowns\runs") },
        @{ name = 'getup-stand'; script = 'getup-stand.ps1'; arguments = @('-Out', "$h\stand"); runs = @("$h\stand\runs") },
        @{ name = 'getup-seeds'; script = 'getup-seeds.ps1'; arguments = @('-Label', $tag); runs = @("Saved\Matrix\getup_seeds\$tag") },
        @{ name = 'natural'; script = 'natural.ps1'; arguments = @('-Label', $tag); runs = @("Saved\Natural\$tag", "Saved\Matrix\getup_seeds\natural_$tag") },
        @{ name = 'compliance'; script = 'compliance.ps1'; arguments = @('-Out', "$h\compliance"); runs = @("$h\compliance\runs") },
        @{ name = 'grab-pulls'; script = 'grab-pulls.ps1'; arguments = @('-Out', "$h\grab_pulls"); runs = @("$h\grab_pulls\runs") },
        @{ name = 'sweep'; script = 'sweep.ps1'; arguments = @('-Name', $tag, '-Scenarios', $getups, '-Vary', 'getup.start_blend_s=0.3,0.6,0'); runs = @("Saved\Sweeps\$tag\runs") },
        @{ name = 'swing-lag'; script = 'swing-lag.ps1'; arguments = @('-Out', "$h\swing_lag"); runs = @("$h\swing_lag") },
        @{ name = 'recording-report'; script = 'recording-report.ps1'; arguments = @('-Name', 'vr_check', '-Out', "$h\recording_report"); runs = @("$h\recording_report") },
        @{ name = 'scenario'; script = 'scenario.ps1'; arguments = @('-Name', 'getup_left', '-Out', "$h\scenario"); runs = @("$h\scenario") },
        @{ name = 'scenario-shot'; script = 'scenario.ps1'; arguments = @('-Name', 'hit_chest_200_falls', '-Shot', '-ShotAt', '3.3', '-Out', "$h\scenario_shot"); runs = @("$h\scenario_shot"); made = @("$h\scenario_shot\final.png", "$h\scenario_shot\shot_3.300.png") },
        @{ name = 'film'; script = 'film.ps1'; arguments = @('-Name', 'hit_chest_200_falls', '-Times', '3.1,3.3,3.5', '-Tag', "${tag}_film"); runs = @("Saved\Films\${tag}_film"); made = @("Saved\Films\${tag}_film\sheet.png") },
        @{ name = 'film-pair'; script = 'film-pair.ps1'; arguments = @('-Name', 'getup_push_weak', '-Around', '8.3', '-Span', '0.3', '-TuneB', 'getup.start_blend_s=0', '-Tag', "${tag}_pair"); runs = @("Saved\Films\${tag}_pair_a", "Saved\Films\${tag}_pair_b"); made = @("Saved\Films\${tag}_pair\pair.mp4"); verdict = '^FILM PAIR ' },
        @{ name = 'record-replay'; script = 'record-replay.ps1'; arguments = @(); runs = @('Saved\Scenarios\demo_hands'); files = @('Saved\Recordings\demo_hands\demo_hands.recording.json') },
        @{ name = 'record-replay-takes'; script = 'record-replay.ps1'; arguments = @('-Takes'); runs = @('Saved\Scenarios\demo_takes', 'Saved\Scenarios\demo_takes_2', 'Saved\Scenarios\demo_takes_3')
            files = @('Saved\Recordings\demo_takes\demo_takes.recording.json', 'Saved\Recordings\demo_takes_2\demo_takes_2.recording.json', 'Saved\Recordings\demo_takes_3\demo_takes_3.recording.json') },
        @{ name = 'record-replay-walk'; script = 'record-replay.ps1'; arguments = @('-Walk', '-Twice'); runs = @('Saved\Scenarios\demo_walk'); files = @('Saved\Recordings\demo_walk\demo_walk.recording.json') },
        @{ name = 'play-smoke'; script = 'play.ps1'; arguments = @('-Smoke', '-Out', "$h\play_smoke") },
        @{ name = 'play-grab'; script = 'play.ps1'; arguments = @('-Demo', '-Grab', '-Out', "$h\play_grab") },
        @{ name = 'surface-friction'; script = 'surface-friction.ps1'; arguments = @('-Out', "$h\surface_friction"); runs = @('Saved\Scenarios\surface_friction_drag') },
        @{ name = 'repeatability'; script = 'repeatability.ps1'; arguments = @('-Name', 'drop_passive', '-Runs', '2'); runs = @('Saved\Scenarios\drop_passive\repeat'); verdict = '^REPEATABILITY ' },
        @{ name = 'matrix-separate'; script = 'push-matrix.ps1'; arguments = @('-Impulses', '30', '-Enforce', '30', '-Bodies', 'spine_04', '-Out', "$h\matrix_separate"); environment = @{ SANDBOX_SEPARATE_PROCESSES = '1' }; runs = @("$h\matrix_separate\runs") },
        @{ name = 'pose-prior'; script = 'pose-prior.ps1'; arguments = @() },
        # The tools of runs as one check set (SP-3): each tool's runs held against that tool's runs alone, run by run.
        @{ name = 'checks'; script = 'checks.ps1'; arguments = @('-What', 'suite,matrix,knockdowns,stand,seeds,compliance,grabs', '-Label', $tag)
            runs = @("Saved\Suites\checks_$tag", "Saved\Checks\$tag\matrix\runs", "Saved\Checks\$tag\knockdowns\runs", "Saved\Checks\$tag\stand\runs",
                "Saved\Matrix\getup_seeds\checks_$tag", "Saved\Checks\$tag\compliance\runs", "Saved\Checks\$tag\grabs\runs")
            like = @('suite', 'push-matrix', 'knockdowns', 'getup-stand', 'getup-seeds', 'compliance', 'grab-pulls') }
    )
    if ($Only) {
        $wanted = @($Only.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ })
        $battery = @($battery | Where-Object { $wanted -contains $_.name })
        if ($battery.Count -eq 0) { Write-Output "BENCH ERROR: -Only names none of the tools of -Hashes"; exit 2 }
    }
    $started = Get-Date
    $tools = @()
    $broken = $null
    foreach ($tool in $battery) {
        # Stale runs of an earlier call must not pass for this one's.
        foreach ($folder in @($tool.runs)) {
            if (-not $folder) { continue }
            $place = Resolve-BenchPath $folder
            if (Test-Path $place) { Remove-Item $place -Recurse -Force }
        }
        try {
            $call = Invoke-BenchTool -Script $tool.script -Arguments $tool.arguments -Environment $tool.environment -Verdict $tool.verdict
        } catch {
            $broken = "$($tool.name): $($_.Exception.Message)"
            break
        }
        $sets = @()
        foreach ($folder in @($tool.runs)) {
            if (-not $folder) { continue }
            $sets += [ordered]@{ folder = $folder.Replace($RepoRoot + '\', ''); runs = (Get-RunHashes (Resolve-BenchPath $folder)) }
        }
        $fileHashes = [ordered]@{}
        foreach ($file in @($tool.files)) {
            if (-not $file) { continue }
            $place = Resolve-BenchPath $file
            if (Test-Path $place) { $fileHashes[$file] = (Get-FileHash $place -Algorithm SHA256).Hash } else { $fileHashes[$file] = $null }
        }
        $madeFiles = [ordered]@{}
        foreach ($file in @($tool.made)) {
            if (-not $file) { continue }
            $madeFiles[$file.Replace($RepoRoot + '\', '')] = (Test-Path (Resolve-BenchPath $file))
        }
        $tools += [ordered]@{ name = $tool.name; call = $call; run_sets = $sets; files = $fileHashes; made = $madeFiles }
    }
    $seconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 0)
    $runCount = 0
    foreach ($tool in $tools) { foreach ($set in $tool.run_sets) { $runCount += $set.runs.Count } }
    $noResult = @($tools | Where-Object { $_.call.exit_code -ne 0 -and $_.call.exit_code -ne 1 })
    Write-JsonFile (Join-Path $work 'hashes.json') ([ordered]@{ label = $Label; date = (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'); commit = $commit; dirty_files = $dirty
        seconds = $seconds; stopped = $broken; tools = $tools })

    $verdictLines = @()
    $exitCode = 0
    if ($broken) { $exitCode = 2 }
    if ($noResult.Count -gt 0) { $exitCode = 2 }
    if ($againstData) {
        # Held against the other battery, tool by tool: the exit code, the verdict line with its times taken out, every run's
        # two hashes, the recordings' files, the pictures and films made.
        # The two batteries' labels are in their folders' names, and so in some verdicts: both are read as one.
        $labelPattern = 'bench_hashes_(' + [regex]::Escape($Label) + '|' + [regex]::Escape($Against) + ')(?![A-Za-z0-9])'
        $normal = { param($text) return ([string]$text -replace $labelPattern, 'bench_hashes_LABEL' -replace '\d+(\.\d+)? s\b', '_ s' -replace '\d+(\.\d+)? ms\b', '_ ms') }
        $sameRuns = 0; $changedRuns = 0; $missingRuns = 0; $newRuns = 0; $sameVerdicts = 0; $compared = 0
        $details = @()
        foreach ($tool in $tools) {
            $like = @(@($battery | Where-Object { $_.name -eq $tool.name })[0].like | Where-Object { $_ })
            if ($like.Count -gt 0) {
                # A set of tools: each of its run sets against the same tool's runs alone in the other battery.
                for ($i = 0; $i -lt $tool.run_sets.Count -and $i -lt $like.Count; $i++) {
                    $alone = @($againstData.tools | Where-Object { $_.name -eq $like[$i] })[0]
                    if (-not $alone -or -not $alone.run_sets) { $details += "  $($tool.name): no runs of $($like[$i]) in '$Against'"; continue }
                    $result = Compare-RunHashes (ConvertTo-RunHashSet @($alone.run_sets)[0].runs) $tool.run_sets[$i].runs
                    $sameRuns += $result.same.Count; $changedRuns += $result.changed.Count; $missingRuns += $result.missing.Count; $newRuns += $result.added.Count
                    foreach ($entry in $result.changed) { $details += "  $($tool.name): changed $($tool.run_sets[$i].folder)\$($entry.run) against $($like[$i]) alone ($($entry.before) -> $($entry.after))" }
                    foreach ($entry in $result.missing) { $details += "  $($tool.name): missing $($tool.run_sets[$i].folder)\$entry" }
                    foreach ($entry in $result.added) { $details += "  $($tool.name): new $($tool.run_sets[$i].folder)\$entry" }
                }
                # Its verdict as the tools' alone in the other battery: the worst of their exit codes.
                $compared++
                $aloneCodes = @($like | ForEach-Object { $name = $_; @($againstData.tools | Where-Object { $_.name -eq $name }) | ForEach-Object { [int]$_.call.exit_code } })
                $expected = 0
                if ($aloneCodes.Count -gt 0) { $expected = ($aloneCodes | Measure-Object -Maximum).Maximum }
                if ([int]$tool.call.exit_code -eq $expected) { $sameVerdicts++ } else { $details += "  $($tool.name): exit $($tool.call.exit_code) where the tools alone gave $expected; '$($tool.call.verdict_line)'" }
                continue
            }
            $old = @($againstData.tools | Where-Object { $_.name -eq $tool.name })[0]
            if (-not $old) { $details += "  $($tool.name): not in '$Against'"; continue }
            $compared++
            # The verdict line kept, or one read again from what the tool printed then.
            $oldVerdict = [string]$old.call.verdict_line
            if (-not $oldVerdict) {
                $oldVerdict = [string]$old.call.first_line
                $pattern = @($battery | Where-Object { $_.name -eq $tool.name })[0].verdict
                if ($old.call.printed -and (Test-Path $old.call.printed)) { $oldVerdict = Get-VerdictLine @(Get-Content $old.call.printed) $pattern }
            }
            $verdictSame = ($old.call.exit_code -eq $tool.call.exit_code) -and ((& $normal $oldVerdict) -eq (& $normal $tool.call.verdict_line))
            if ($verdictSame) { $sameVerdicts++ } else {
                $details += "  $($tool.name): verdict exit $($old.call.exit_code) '$oldVerdict'"
                $details += "  $(' ' * $tool.name.Length)  -> exit $($tool.call.exit_code) '$($tool.call.verdict_line)'"
            }
            for ($i = 0; $i -lt $tool.run_sets.Count; $i++) {
                $oldSet = $null
                if ($old.run_sets -and @($old.run_sets).Count -gt $i) { $oldSet = @($old.run_sets)[$i] }
                if (-not $oldSet) { $newRuns += $tool.run_sets[$i].runs.Count; continue }
                $result = Compare-RunHashes (ConvertTo-RunHashSet $oldSet.runs) $tool.run_sets[$i].runs
                $sameRuns += $result.same.Count; $changedRuns += $result.changed.Count; $missingRuns += $result.missing.Count; $newRuns += $result.added.Count
                foreach ($entry in $result.changed) { $details += "  $($tool.name): changed $($tool.run_sets[$i].folder)\$($entry.run) ($($entry.before) -> $($entry.after))" }
                foreach ($entry in $result.missing) { $details += "  $($tool.name): missing $($tool.run_sets[$i].folder)\$entry" }
                foreach ($entry in $result.added) { $details += "  $($tool.name): new $($tool.run_sets[$i].folder)\$entry" }
                if ($result.same.Count -eq 0 -and $tool.run_sets[$i].runs.Count -eq 0) { $details += "  $($tool.name): no run in $($tool.run_sets[$i].folder)" }
            }
            foreach ($key in @($tool.files.Keys)) {
                $was = $null
                if ($old.files -and $old.files.PSObject.Properties[$key]) { $was = $old.files.PSObject.Properties[$key].Value }
                if ($was -ne $tool.files[$key] -or $null -eq $was) { $details += "  $($tool.name): file $key $was -> $($tool.files[$key])"; $changedRuns++ } else { $sameRuns++ }
            }
            foreach ($key in @($tool.made.Keys)) { if (-not $tool.made[$key]) { $details += "  $($tool.name): made no $key" } }
        }
        $verdictWord = 'SAME'
        if ($changedRuns -gt 0 -or $missingRuns -gt 0 -or $sameVerdicts -ne $compared) { $verdictWord = 'DIFFERENT'; if ($exitCode -eq 0) { $exitCode = 1 } }
        if ($broken -or $noResult.Count -gt 0) { $verdictWord = 'INCOMPLETE' }
        Write-Output ("BENCH HASHES {0} against {1}: {2}; {3} run(s) and file(s) with the same hashes, {4} changed, {5} missing, {6} new; verdicts the same in {7} of {8} tool(s); {9} s" -f `
            $Label, $Against, $verdictWord, $sameRuns, $changedRuns, $missingRuns, $newRuns, $sameVerdicts, $compared, $seconds)
        $details | ForEach-Object { Write-Output $_ }
    } else {
        $word = 'KEPT'
        if ($broken -or $noResult.Count -gt 0) { $word = 'INCOMPLETE' }
        Write-Output ("BENCH HASHES {0}: {1}; {2} tool(s) ran, {3} run(s) kept in {4} s; {5}" -f $Label, $word, $tools.Count, $runCount, $seconds, (Join-Path $work 'hashes.json'))
    }
    if ($broken) { Write-Output "  stopped at $broken" }
    foreach ($tool in $tools) {
        $runsOf = 0
        foreach ($set in $tool.run_sets) { $runsOf += $set.runs.Count }
        Write-Output ('  {0,-20} exit {1}  {2,4} run(s) {3,7:N1} s  {4}' -f $tool.name, $tool.call.exit_code, $runsOf, [double]$tool.call.seconds, $tool.call.verdict_line)
    }
    exit $exitCode
}

# --- The times ------------------------------------------------------------------------------------------------------------

$controllerCpp = Join-Path $RepoRoot 'Source\SandboxBody\Private\SandboxBodyController.cpp'
$controllerHeader = Join-Path $RepoRoot 'Source\SandboxBody\Public\SandboxBodyController.h'
foreach ($source in @($controllerCpp, $controllerHeader)) { if (-not (Test-Path $source)) { Write-Output "BENCH ERROR: $source is not there"; exit 2 } }
$suiteLabel = "bench_$Label"
$getupScenarios = 'getup_back,getup_front,getup_left,getup_right'
$rowList = @(
    @{ name = 'one_scenario'; what = 'scenario.ps1 -Name getup_left'; steps = @(@{ script = 'scenario.ps1'; arguments = @('-Name', 'getup_left', '-Out', (Join-Path $work 'runs\one_scenario')) }) },
    @{ name = 'sweep_4x3'; what = 'sweep.ps1, 4 get-ups times getup.start_blend_s 0.3, 0.6, 0'; steps = @(@{ script = 'sweep.ps1'; arguments = @('-Name', $suiteLabel, '-Scenarios', $getupScenarios, '-Vary', 'getup.start_blend_s=0.3,0.6,0') }) },
    @{ name = 'test'; what = 'test.ps1'; steps = @(@{ script = 'test.ps1'; arguments = @() }) },
    @{ name = 'suite'; what = 'suite.ps1'; steps = @(@{ script = 'suite.ps1'; arguments = @('-Label', $suiteLabel) }) },
    @{ name = 'edit_build_tests_suite'; what = 'the controller''s .cpp stamped, build.ps1, test.ps1, suite.ps1'; touch = $controllerCpp
        steps = @(@{ script = 'build.ps1'; arguments = @() }, @{ script = 'test.ps1'; arguments = @() }, @{ script = 'suite.ps1'; arguments = @('-Label', $suiteLabel) }) },
    @{ name = 'getup_seeds'; what = 'getup-seeds.ps1, three seeds'; steps = @(@{ script = 'getup-seeds.ps1'; arguments = @('-Label', $suiteLabel) }) },
    @{ name = 'natural_quick'; what = 'natural.ps1 -Quick'; steps = @(@{ script = 'natural.ps1'; arguments = @('-Quick', '-Label', $suiteLabel) }) }
)
if ($Builds) {
    $rowList += @(
        @{ name = 'build_cpp'; what = 'build.ps1 after the controller''s .cpp is stamped'; touch = $controllerCpp; build = $true; steps = @(@{ script = 'build.ps1'; arguments = @('-Timestamps') }) },
        @{ name = 'build_header'; what = 'build.ps1 after SandboxBodyController.h is stamped'; touch = $controllerHeader; build = $true; steps = @(@{ script = 'build.ps1'; arguments = @('-Timestamps') }) },
        @{ name = 'build_full'; what = 'build.ps1 -Rebuild'; build = $true; steps = @(@{ script = 'build.ps1'; arguments = @('-Rebuild', '-Timestamps') }) }
    )
}
if ($Only) {
    $wanted = @($Only.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    $rowList = @($rowList | Where-Object { $wanted -contains $_.name })
    if ($rowList.Count -eq 0) { Write-Output 'BENCH ERROR: -Only names none of the measurements'; exit 2 }
}

$started = Get-Date
$measured = [ordered]@{}
foreach ($row in $rowList) {
    $measured[$row.name] = [ordered]@{ what = $row.what; seconds = @(); exit_codes = @(); first_lines = @(); parts = @(); compiles = @() }
}
$stopped = $null
:passes for ($pass = 1; $pass -le $Passes; $pass++) {
    foreach ($row in $rowList) {
        Add-BenchNote "pass $pass, $($row.name)"
        if ($row.touch) { Set-SourceStamp $row.touch }
        $rowStarted = Get-Date
        $calls = @()
        try {
            foreach ($step in $row.steps) {
                $pattern = ''
                if ($step.script -eq 'suite.ps1') { $pattern = '^SUITE ' }
                $calls += (Invoke-BenchTool -Script $step.script -Arguments $step.arguments -Verdict $pattern)
            }
        } catch {
            $stopped = "pass $pass, $($row.name): $($_.Exception.Message)"
            break passes
        }
        $total = [Math]::Round(((Get-Date) - $rowStarted).TotalSeconds, 2)
        $entry = $measured[$row.name]
        $entry.seconds += $total
        $entry.exit_codes += , @($calls | ForEach-Object { $_.exit_code })
        $entry.first_lines += , @($calls | ForEach-Object { $_.verdict_line })
        $entry.parts += , @($calls | ForEach-Object { [ordered]@{ script = $_.script; seconds = $_.seconds } })
        if ($row.build) {
            # Each compile's own time (build.json's actions, wall_s; UnrealBuildTool prints them when
            # Saved\UnrealBuildTool\BuildConfiguration.xml asks it to), and its end from the executor's start where the log has it.
            $built = Read-JsonFile (Join-Path $ToolsOut 'build.json')
            $entry.compiles += , @($built.actions | Where-Object { $_.action -match '^Compile' } | ForEach-Object { [ordered]@{ file = ($_.action -replace '^Compile \[\w+\] ', ''); wall_s = $_.wall_s; finished_s = $_.finished_s } })
        }
    }
}
$seconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 0)

$fullRun = $null
if ($Full -and -not $stopped) {
    Add-BenchNote 'bootstrap'
    try { $fullRun = Invoke-BenchTool -Script 'bootstrap.ps1' -Arguments @() -Verdict '^BOOTSTRAP ' } catch { $stopped = "bootstrap: $($_.Exception.Message)" }
    if ($fullRun) {
        $steps = Read-JsonFile (Join-Path $ToolsOut 'bootstrap.json')
        $fullRun.steps = @($steps.steps)
    }
}

$rowsOut = [ordered]@{}
$noResult = @()
foreach ($name in $measured.Keys) {
    $entry = $measured[$name]
    $entry.median_s = Get-Median $entry.seconds
    # Each part's median (the build, the tests and the suite of the edit's row).
    $partMedians = @()
    if ($entry.parts.Count -gt 0) {
        for ($i = 0; $i -lt @($entry.parts[0]).Count; $i++) {
            $partMedians += [ordered]@{ script = @($entry.parts[0])[$i].script; median_s = (Get-Median @($entry.parts | ForEach-Object { @($_)[$i].seconds })) }
        }
    }
    $entry.part_medians = $partMedians
    foreach ($codes in $entry.exit_codes) { foreach ($code in @($codes)) { if ($code -ne 0 -and $code -ne 1) { $noResult += $name } } }
    $rowsOut[$name] = $entry
}
$ok = (-not $stopped) -and ($noResult.Count -eq 0)
Write-JsonFile (Join-Path $benchRoot "$Label.json") ([ordered]@{
    label = $Label; date = (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'); commit = $commit; dirty_files = $dirty; free_gb_at_start = $freeAtStart
    passes = $Passes; seconds = $seconds; ok = $ok; stopped = $stopped; rows = $rowsOut; bootstrap = $fullRun
})

$word = 'OK'
if ($stopped) { $word = 'STOPPED' } elseif ($noResult.Count -gt 0) { $word = 'ERROR' }
Write-Output ("BENCH {0}: {1}, {2} measurement(s), {3} pass(es) in {4} s; {5}" -f $word, $Label, $rowList.Count, $Passes, $seconds, (Join-Path $benchRoot "$Label.json"))
if ($stopped) { Write-Output "  stopped at $stopped" }
if ($noResult.Count -gt 0) { Write-Output "  a tool gave no result in: $(@($noResult | Select-Object -Unique) -join ', ')" }
$header = '  {0,-24} {1,9}  {2,-26}' -f 'measurement', 'median s', 'passes s'
if ($againstData) { $header += (' {0,12} {1,8}' -f "$Against s", 'change') }
Write-Output $header
foreach ($name in $rowsOut.Keys) {
    $entry = $rowsOut[$name]
    $line = '  {0,-24} {1,9:N1}  {2,-26}' -f $name, [double]$entry.median_s, (($entry.seconds | ForEach-Object { '{0:N1}' -f [double]$_ }) -join ' ')
    if ($againstData) {
        $old = $null
        if ($againstData.rows.PSObject.Properties[$name]) { $old = $againstData.rows.PSObject.Properties[$name].Value.median_s }
        if ($null -ne $old) { $line += (' {0,12:N1} {1,7:N0}%' -f [double]$old, (100.0 * ([double]$entry.median_s - [double]$old) / [double]$old)) } else { $line += (' {0,12}' -f '-') }
    }
    Write-Output $line
    if (@($entry.part_medians).Count -gt 1) {
        foreach ($part in $entry.part_medians) { Write-Output ('      {0,-20} {1,9:N1}' -f $part.script, [double]$part.median_s) }
    }
    if (@($entry.compiles).Count -gt 0) {
        $last = @($entry.compiles)[-1]
        $slowest = @($last | Where-Object { $null -ne $_.wall_s } | Sort-Object { -[double]$_.wall_s } | Select-Object -First 4 | ForEach-Object { '{0} {1:N1} s' -f $_.file, [double]$_.wall_s })
        if ($slowest.Count -eq 0) { $slowest = @('no times: Saved\UnrealBuildTool\BuildConfiguration.xml does not ask for them') }
        Write-Output ('      {0} compile(s) in the last pass; the longest: {1}' -f @($last).Count, ($slowest -join ', '))
    }
    $codesText = @($entry.exit_codes | ForEach-Object { @($_) -join '/' }) -join ' '
    if ($codesText -match '[^0/ ]') { Write-Output "      exit codes: $codesText; first lines: $(@($entry.first_lines)[-1] -join ' | ')" }
}
if ($fullRun) {
    $line = '  {0,-24} {1,9:N1}  exit {2}: {3}' -f 'bootstrap', [double]$fullRun.seconds, $fullRun.exit_code, $fullRun.verdict_line
    if ($againstData -and $againstData.bootstrap) { $line += (' (was {0:N1} s)' -f [double]$againstData.bootstrap.seconds) }
    Write-Output $line
}
if ($ok) { exit 0 }
exit 2
