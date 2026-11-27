# Takes a clean clone to a verified build: stock content, the clips of Epic's Game Animation Sample, compile, asset generation, lighting bake, unit tests,
# scenario suite, push matrix, knockdowns, the stand of falls and get-ups, self-test of the interactive session,
# a session's hands recorded and replayed, the grab's session, and the headset's walk with a made-up headset.
#   powershell -ExecutionPolicy Bypass -File Tools\bootstrap.ps1
# Needs: Epic's Game Animation Sample as a project of its own (a manual download; Tools\fetch-sample-anims.ps1 says
# where it is looked for), Unreal Engine 5.8 (Epic Games Launcher), Visual Studio 2026 with the MSVC 14.50 toolset and
# Windows SDK 10.0.22621.
# The first six steps run one after another and the bootstrap stops at the first that fails. Then the suite, the push matrix,
# the knockdowns and the stand run as one check set (Tools\checks.ps1: one queue of eight engine processes, the longest jobs
# first), and beside it, as a ninth process, the sessions one at a time: the self-test, the three records and replays and the
# grab's session (docs/fast-checks.md, SP-3). Every step is decided by its result file, not by its exit code: each is removed
# before its step and read after it. Writes Saved\Tools\bootstrap.json, one entry a step, with its verdict line and its
# seconds (a step of the set from its first job's start to its verdict, so those steps overlap). Exit code: 0 every step
# passed, 1 not.
. "$PSScriptRoot\common.ps1"

# What decides each step: its result file, and what in it says that it passed.
$okField = { param($r) return [bool]$r.ok }
$steps = @(
    @{ name = 'fetch-stock-content'; script = 'fetch-stock-content.ps1'; result = 'Saved\Tools\fetch-stock-content.json'; passed = $okField },
    @{ name = 'fetch-sample-anims';  script = 'fetch-sample-anims.ps1'; result = 'Saved\Tools\fetch-sample-anims.json'; passed = $okField },
    @{ name = 'build';               script = 'build.ps1'; result = 'Saved\Tools\build.json'; passed = $okField },
    @{ name = 'gen-assets';          script = 'gen-assets.ps1'; result = 'Saved\Tools\gen-assets.json'; passed = $okField },
    @{ name = 'bake-lighting';       script = 'bake-lighting.ps1'; result = 'Saved\Tools\bake-lighting.json'; passed = $okField },
    @{ name = 'test';                script = 'test.ps1'; result = 'Saved\Tools\test.json'; passed = $okField },
    @{ name = 'suite';               check = 'suite'; result = 'Saved\Tools\suite.json'; passed = $okField },
    @{ name = 'push-matrix';         check = 'matrix'; result = 'Saved\Matrix\push\matrix.json'; passed = $okField },
    @{ name = 'knockdowns';          check = 'knockdowns'; result = 'Saved\Matrix\knockdowns\knockdowns.json'; passed = $okField },
    @{ name = 'getup-stand';         check = 'stand'; result = 'Saved\Matrix\getup_stand\getup-stand.json'; passed = { param($r) return [bool]$r.pass } },
    @{ name = 'play-smoke';          script = 'play.ps1'; arguments = @('-Smoke'); result = 'Saved\Play\room_stand_npc\play-result.json'; passed = $okField },
    @{ name = 'record-replay';       script = 'record-replay.ps1'; arguments = @('-Twice'); result = 'Saved\Tools\record-replay.json'; passed = $okField },
    @{ name = 'record-replay-takes'; script = 'record-replay.ps1'; arguments = @('-Takes'); result = 'Saved\Tools\record-replay-takes.json'; passed = $okField },
    @{ name = 'play-grab';           script = 'play.ps1'; arguments = @('-Demo', '-Grab'); result = 'Saved\Play\room_stand_npc\play-result.json'; passed = $okField },
    # The headset's way without a person or a headset (FR-42): a made-up headset walks the player's body into the
    # character, along it, turns and walks away; checked, recorded and replayed twice.
    @{ name = 'record-replay-walk';  script = 'record-replay.ps1'; arguments = @('-Walk', '-Twice'); result = 'Saved\Tools\record-replay-walk.json'; passed = $okField }
)
$started = Get-Date
$results = [ordered]@{}
# The result of an earlier bootstrap must not pass for this one's (Tools\bench.ps1 -Full reads it).
$bootstrapFile = Join-Path $ToolsOut 'bootstrap.json'
if (Test-Path $bootstrapFile) { Remove-Item $bootstrapFile -Force }

function Get-StepVerdict($Step, [string[]]$Lines) {
    # The step's verdict line: the suite's is its last, every other tool's its first.
    $all = @($Lines | ForEach-Object { [string]$_ })
    if ($Step.name -eq 'suite') { $last = @($all | Where-Object { $_ -match '^SUITE ' }) | Select-Object -Last 1; if ($last) { return [string]$last } }
    # Not a note or warning of the queue that came before it.
    $first = @($all | Where-Object { $_ -notmatch '^QUEUE ' }) | Select-Object -First 1
    if ($first) { return [string]$first }
    return ''
}
function Test-StepResult($Step) {
    # Passed by its result file alone; a file that is missing or cannot be read is a step that did not pass.
    $file = Join-Path $RepoRoot $Step.result
    $read = $null
    try { $read = Read-JsonFile $file } catch { $read = $null }
    if (-not $read) { return $false }
    return [bool](& $Step.passed $read)
}
function Clear-StepResult($Step) {
    $file = Join-Path $RepoRoot $Step.result
    if (Test-Path $file) { Remove-Item $file -Force }
}
function Wait-AloneRuns {
    # A run of the check set that has to be alone for its timing (the suite's second run over the step limit) waits for the
    # session that runs beside the set; the next session waits for that run in turn.
    $queueRoot = Join-Path $RepoRoot 'Saved\Queue'
    while (@(Get-ChildItem $queueRoot -Directory -Filter 'check_*' -ErrorAction SilentlyContinue | ForEach-Object { Get-ChildItem $_.FullName -Filter 'alone_*.request' -File -ErrorAction SilentlyContinue }).Count -gt 0) {
        Start-Sleep -Milliseconds 500
    }
}
function Invoke-Step($Step) {
    if ($Step.lane) { Wait-AloneRuns }
    Write-Output "=== $($Step.name) ==="
    Clear-StepResult $Step
    $stepStarted = Get-Date
    $stepArguments = @()
    if ($Step.arguments) { $stepArguments = $Step.arguments }
    $lines = @(& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot $Step.script) @stepArguments)
    $code = $LASTEXITCODE
    $lines | ForEach-Object { Write-Output $_ }
    $script:results[$Step.name] = [ordered]@{ step = $Step.name; ok = (Test-StepResult $Step); exit_code = $code; verdict = (Get-StepVerdict $Step $lines)
        seconds = [math]::Round(((Get-Date) - $stepStarted).TotalSeconds, 1); started_s = [math]::Round(($stepStarted - $script:started).TotalSeconds, 1) }
}

# --- One after another, up to the tests --------------------------------------------------------------------------------------
$ok = $true
foreach ($step in @($steps | Where-Object { $_.script -and @('fetch-stock-content', 'fetch-sample-anims', 'build', 'gen-assets', 'bake-lighting', 'test') -contains $_.name })) {
    Invoke-Step $step
    if (-not $results[$step.name].ok) { $ok = $false; break }
}

if ($ok) {
    # --- The check set, and the sessions beside it as the ninth process ------------------------------------------------------
    $checked = @($steps | Where-Object { $_.check })
    foreach ($step in $checked) { Clear-StepResult $step }
    $checksPrinted = Join-Path $ToolsOut 'bootstrap-checks.txt'
    $checksResult = Join-Path $RepoRoot 'Saved\Checks\latest\checks.json'
    if (Test-Path $checksResult) { Remove-Item $checksResult -Force }
    $setStarted = Get-Date
    $what = (@($checked | ForEach-Object { $_.check }) -join ',')
    $checks = Start-Process powershell -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $PSScriptRoot 'checks.ps1')`" -What $what" `
        -RedirectStandardOutput $checksPrinted -RedirectStandardError (Join-Path $ToolsOut 'bootstrap-checks.err.txt') -WindowStyle Hidden -PassThru
    $null = $checks.Handle
    Write-Output "=== $(@($checked | ForEach-Object { $_.name }) -join ', ') (one check set, Tools\checks.ps1 -What $what; printed when it ends) ==="
    foreach ($step in @($steps | Where-Object { $_.script -and @('play-smoke', 'record-replay', 'record-replay-takes', 'play-grab', 'record-replay-walk') -contains $_.name })) {
        $step.lane = $true
        Invoke-Step $step
    }
    $checks.WaitForExit()
    Write-Output "=== the check set ==="
    if (Test-Path $checksPrinted) { Get-Content $checksPrinted | ForEach-Object { Write-Output $_ } }
    $set = Read-JsonFile $checksResult
    foreach ($step in $checked) {
        $tool = $null
        if ($set) { $tool = @($set.tools | Where-Object { $_.tool -eq $step.check })[0] }
        $lines = @()
        if ($tool -and $tool.printed -and (Test-Path $tool.printed)) { $lines = @(Get-Content $tool.printed) }
        $entry = [ordered]@{ step = $step.name; ok = (Test-StepResult $step); exit_code = $(if ($tool) { $tool.exit_code } else { $null }); verdict = (Get-StepVerdict $step $lines)
            seconds = $(if ($tool) { $tool.seconds } else { $null }); started_s = [math]::Round(($setStarted - $started).TotalSeconds, 1) }
        $results[$step.name] = $entry
    }
}

# In the order of the steps, whatever ran when.
$ordered = @()
foreach ($step in $steps) { if ($results.Contains($step.name)) { $ordered += $results[$step.name] } }
$failedSteps = @($ordered | Where-Object { -not $_.ok })
$allOk = ($ordered.Count -eq $steps.Count) -and ($failedSteps.Count -eq 0)
$seconds = [math]::Round(((Get-Date) - $started).TotalSeconds, 1)
Write-JsonFile (Join-Path $ToolsOut 'bootstrap.json') ([ordered]@{ ok = $allOk; seconds = $seconds; steps = $ordered })
foreach ($entry in $ordered) {
    $mark = 'ok  '
    if (-not $entry.ok) { $mark = 'FAIL' }
    Write-Output ('  [{0}] {1,-20} {2,7} s  {3}' -f $mark, $entry.step, $(if ($null -ne $entry.seconds) { '{0:N1}' -f [double]$entry.seconds } else { '-' }), $entry.verdict)
}
if ($allOk) {
    Write-Output "BOOTSTRAP OK: all $($steps.Count) steps passed in $seconds s"
    exit 0
}
if ($failedSteps.Count -gt 0) { Write-Output "BOOTSTRAP FAILED at step(s) $(@($failedSteps | ForEach-Object { "'$($_.step)'" }) -join ', ') ($seconds s)" }
else { Write-Output "BOOTSTRAP FAILED: $($steps.Count - $ordered.Count) step(s) did not run ($seconds s)" }
exit 1
