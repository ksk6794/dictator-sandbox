# Several tools of runs through one queue (docs/fast-checks.md, SP-3): the plans of every tool asked for go into one queue of
# engine processes, the longest jobs first whichever tool planned them, and each tool gets the rounds it asks for (the stand's
# draws again, the grab's probes and pulls); then every tool's verdict. In place of a battery that runs the tools one after
# another, each starting its processes again.
#   powershell -ExecutionPolicy Bypass -File Tools\checks.ps1 -What suite,matrix,knockdowns,seeds
#   ... -What "..."              of: suite, matrix, knockdowns, stand, seeds, compliance, natural, natural_quick, grabs
#   ... -Tune "name=value,..."   tuning overrides for every run of every tool (needs -Label)
#   ... -Label <name>            every tool writes into a folder of its own with this label (Saved\Checks\<label>\<tool>, the
#                                suite into Saved\Suites\checks_<label>, the seeds into Saved\Matrix\getup_seeds\checks_<label>,
#                                the scorecard into Saved\Natural\checks_<label>) and leaves the tools' usual folders, which
#                                baselines are made of, as they were. Without it each tool writes where it always does
#   ... -Parallel 8              engine processes of the queue
# Each tool runs as it does alone, as a process of its own; inside the set its runs go to the set's queue instead of processes
# of its own (Invoke-ScenarioBatch; the suite's second run of a scenario over the step limit runs alone after the queue has
# emptied, as it does alone). What each printed is kept in Saved\Checks\<label or 'latest'>\<tool>.txt.
# Prints the verdict of the set first, then each tool's verdict line and its seconds (from its first job's start to its
# verdict); writes Saved\Checks\<label or 'latest'>\checks.json. Exit code: 0 every tool passed, 1 a tool did not, 2 a tool
# gave no result or the set could not run.
param(
    [Parameter(Mandatory = $true)][string]$What,
    [string]$Tune,
    [string]$Label,
    [int]$Parallel = 8
)
. "$PSScriptRoot\common.ps1"

if ($Label -and $Label -notmatch '^[A-Za-z0-9_\-\.]+$') { Write-Output "CHECKS ERROR: -Label takes letters, digits, '_', '-' and '.', not '$Label'"; exit 2 }
if ($Tune -and -not $Label) { Write-Output 'CHECKS ERROR: -Tune needs -Label: tuned runs must not land where baselines are made of'; exit 2 }
if ($env:SANDBOX_CHECK_QUEUE) { Write-Output 'CHECKS ERROR: a check set runs inside another one'; exit 2 }
$setName = 'latest'
if ($Label) { $setName = $Label }
$work = Join-Path $RepoRoot "Saved\Checks\$setName"
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force $work | Out-Null

# The tools, each with the arguments that put its output under the label, and the result file it is judged by: whether it
# passed is read there (ok, pass, or for the tools that measure and judge nothing, that it is there), never from an exit code
# alone; a tool without its result file gave no result.
$okField = { param($r) return [bool]$r.ok }
$passField = { param($r) return [bool]$r.pass }
$isThere = { param($r) return $true }
function Get-CheckTool([string]$Name) {
    $own = $null
    if ($Label) { $own = Join-Path $work $Name }
    $folder = { param($default) if ($own) { return $own } else { return (Join-Path $RepoRoot $default) } }
    switch ($Name) {
        'suite' {
            if ($Label) { return @{ script = 'suite.ps1'; arguments = @('-Label', "checks_$Label"); verdict = '^SUITE '; result = (Join-Path $RepoRoot "Saved\Suites\checks_$Label\suite.json"); passed = $okField } }
            return @{ script = 'suite.ps1'; arguments = @(); verdict = '^SUITE '; result = (Join-Path $ToolsOut 'suite.json'); passed = $okField }
        }
        'matrix' { $out = & $folder 'Saved\Matrix\push'; return @{ script = 'push-matrix.ps1'; arguments = @('-Out', $out); result = (Join-Path $out 'matrix.json'); passed = $okField } }
        'knockdowns' { $out = & $folder 'Saved\Matrix\knockdowns'; return @{ script = 'knockdowns.ps1'; arguments = @('-Out', $out); result = (Join-Path $out 'knockdowns.json'); passed = $okField } }
        'stand' { $out = & $folder 'Saved\Matrix\getup_stand'; return @{ script = 'getup-stand.ps1'; arguments = @('-Out', $out); result = (Join-Path $out 'getup-stand.json'); passed = $passField } }
        'seeds' {
            $seedLabel = 'seeds'
            if ($Label) { $seedLabel = "checks_$Label" }
            return @{ script = 'getup-seeds.ps1'; arguments = @('-Label', $seedLabel); result = (Join-Path $RepoRoot "Saved\Matrix\getup_seeds\$seedLabel\getup-seeds.json"); passed = $passField }
        }
        'compliance' { $out = & $folder 'Saved\Matrix\compliance'; return @{ script = 'compliance.ps1'; arguments = @('-Out', $out); result = (Join-Path $out 'compliance.json'); passed = $isThere } }
        'natural' {
            $naturalLabel = 'latest'
            if ($Label) { $naturalLabel = "checks_$Label" }
            return @{ script = 'natural.ps1'; arguments = @('-Label', $naturalLabel); result = (Join-Path $RepoRoot "Saved\Natural\$naturalLabel\report.json"); passed = $isThere }
        }
        'natural_quick' {
            # Not the full scorecard's folder: both may be asked for in one set.
            $naturalLabel = 'latest_quick'
            if ($Label) { $naturalLabel = "checks_quick_$Label" }
            return @{ script = 'natural.ps1'; arguments = @('-Quick', '-Label', $naturalLabel); result = (Join-Path $RepoRoot "Saved\Natural\$naturalLabel\report.json"); passed = $isThere }
        }
        'grabs' { $out = & $folder 'Saved\Matrix\grab_pulls'; return @{ script = 'grab-pulls.ps1'; arguments = @('-Out', $out); result = (Join-Path $out 'grab_pulls.json'); passed = $okField } }
    }
    return $null
}
$names = @($What.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ } | Select-Object -Unique)
if ($names.Count -eq 0) { Write-Output 'CHECKS ERROR: -What names no tool'; exit 2 }
foreach ($name in $names) {
    if (-not (Get-CheckTool $name)) { Write-Output "CHECKS ERROR: '$name' is no tool of a check set (suite, matrix, knockdowns, stand, seeds, compliance, natural, natural_quick, grabs)"; exit 2 }
}
$started = Get-Date
$queue = Enter-CheckQueue -Parallel $Parallel
$children = @()
foreach ($name in $names) {
    $tool = Get-CheckTool $name
    $arguments = @($tool.arguments)
    if ($Tune) { $arguments += @('-Tune', $Tune) }
    $argumentText = "-NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $PSScriptRoot $tool.script)`""
    foreach ($argument in $arguments) {
        if ($argument -match '^-[A-Za-z]+$') { $argumentText += " $argument" } else { $argumentText += " `"$([string]$argument)`"" }
    }
    $printed = Join-Path $work "$name.txt"
    # The result it is judged by is removed first, so that an earlier one never passes for it.
    if (Test-Path $tool.result) { Remove-Item $tool.result -Force }
    $env:SANDBOX_CHECK_TOOL = $name
    $process = Start-Process powershell -ArgumentList $argumentText -RedirectStandardOutput $printed -RedirectStandardError (Join-Path $work "$name.err.txt") -WindowStyle Hidden -PassThru
    # The handle is held, or the exit code of a process started this way is lost once it ends.
    $null = $process.Handle
    $children += [pscustomobject]@{ name = $name; tool = $tool; process = $process; printed = $printed; started = (Get-Date) }
}
$env:SANDBOX_CHECK_TOOL = $null
Wait-CheckChildren -Children @($children | ForEach-Object { $_.process })
$firstJobs = Get-CheckQueueFirstJobs
$queueEnd = Exit-CheckQueue
$seconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 1)

$results = @()
foreach ($child in $children) {
    $lines = @()
    if (Test-Path $child.printed) { $lines = @(Get-Content $child.printed) }
    $verdict = ''
    if ($child.tool.verdict) { $verdict = [string](@($lines | Where-Object { $_ -match $child.tool.verdict }) | Select-Object -Last 1) }
    # Its first line, not a note of the queue.
    if (-not $verdict) { $verdict = [string](@($lines | Where-Object { $_ -notmatch '^QUEUE ' }) | Select-Object -First 1) }
    $from = $child.started
    if ($firstJobs.Contains($child.name)) { $from = $firstJobs[$child.name] }
    # Passed, failed, or no result, by its result file.
    $read = $null
    try { $read = Read-JsonFile $child.tool.result } catch { $read = $null }
    $outcome = 'error'
    if ($read) { if (& $child.tool.passed $read) { $outcome = 'ok' } else { $outcome = 'failed' } }
    $results += [ordered]@{
        tool = $child.name; script = $child.tool.script; outcome = $outcome; result = $child.tool.result; exit_code = $child.process.ExitCode; verdict = $verdict
        seconds = [Math]::Round(($child.process.ExitTime - $from).TotalSeconds, 1)
        seconds_from_start = [Math]::Round(($child.process.ExitTime - $started).TotalSeconds, 1)
        printed = $child.printed
    }
}
$failed = @($results | Where-Object { $_.outcome -eq 'failed' })
$broken = @($results | Where-Object { $_.outcome -eq 'error' })
$lostNote = ''
if ($queueEnd -and $queueEnd.lost -gt 0) { $lostNote = "; $($queueEnd.lost) job(s) lost, logs kept in $($queueEnd.dir)" }
$started_processes = 0
$most = 0
if ($queueEnd) { $started_processes = $queueEnd.processes; $most = $queueEnd.most }
Write-JsonFile (Join-Path $work 'checks.json') ([ordered]@{
    ok = ($failed.Count -eq 0 -and $broken.Count -eq 0); what = $names; tune = $Tune; label = $Label; parallel = $Parallel; seconds = $seconds
    engine_processes_started = $started_processes; engine_processes_at_once = $most; lost_jobs = $(if ($queueEnd) { $queueEnd.lost } else { 0 }); tools = $results
})
$word = 'OK'
if ($broken.Count -gt 0) { $word = 'ERROR' } elseif ($failed.Count -gt 0) { $word = 'FAILED' }
$summary = @($results | ForEach-Object { '{0} {1}' -f $_.tool, $(if ($_.outcome -eq 'ok') { 'ok' } elseif ($_.outcome -eq 'failed') { 'FAILED' } else { "ERROR (no result, exit $($_.exit_code))" }) }) -join ', '
Write-Output ("CHECKS {0}: {1} tool(s) in {2} s through one queue ({3} engine process(es) started, {4} at once at most): {5}{6}" -f $word, $results.Count, $seconds, $started_processes, $most, $summary, $lostNote)
foreach ($result in $results) {
    Write-Output ('  {0,-14} exit {1} {2,7:N1} s  {3}' -f $result.tool, $result.exit_code, [double]$result.seconds, $result.verdict)
}
if ($Tune) { Write-Output "  tune: $Tune" }
Write-Output "  printed: $work"
if ($broken.Count -gt 0) { exit 2 }
if ($failed.Count -gt 0) { exit 1 }
exit 0
