# Runs every scenario in Scenarios\ headless and prints one line per scenario.
#   powershell -ExecutionPolicy Bypass -File Tools\suite.ps1 [-Filter drop*]
# Writes Saved\Tools\suite.json. Exit code: 0 all pass, 1 at least one failed or could not run.
param([string]$Filter = '*')
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
    $results += [ordered]@{ scenario = $name; pass = ($code -eq 0); exit_code = $code; failed_checks = $failedChecks }
}

$failed = @($results | Where-Object { -not $_.pass })
Write-JsonFile (Join-Path $ToolsOut 'suite.json') ([ordered]@{
    ok      = ($failed.Count -eq 0)
    total   = $results.Count
    passed  = $results.Count - $failed.Count
    results = $results
})

if ($failed.Count -eq 0) {
    Write-Output "SUITE OK: $($results.Count) scenario(s) passed"
    exit 0
}
Write-Output "SUITE FAILED: $($failed.Count) of $($results.Count) scenario(s) did not pass"
foreach ($entry in $failed) {
    Write-Output "  $($entry.scenario): $($entry.failed_checks -join ', ')"
}
exit 1
