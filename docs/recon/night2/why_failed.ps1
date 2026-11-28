# Prints, for each scenario named (or every one under Saved\Scenarios that did not pass), the checks it failed.
#   powershell -ExecutionPolicy Bypass -File docs\recon\night2\why_failed.ps1 [-Names "a,b"]
param([string]$Names = '')
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$dirs = if ($Names) { $Names.Split(',') | ForEach-Object { Join-Path $repo "Saved\Scenarios\$($_.Trim())" } }
        else { Get-ChildItem (Join-Path $repo 'Saved\Scenarios') -Directory | ForEach-Object { $_.FullName } }
foreach ($dir in $dirs) {
    $file = Join-Path $dir 'metrics.json'
    if (-not (Test-Path $file)) { continue }
    $m = Get-Content $file -Raw | ConvertFrom-Json
    if (-not $Names -and $m.pass) { continue }
    Write-Output "$($m.scenario): pass=$($m.pass)"
    foreach ($c in $m.checks) {
        if (-not $c.pass) { Write-Output ("   " + ($c | ConvertTo-Json -Compress -Depth 4)) }
    }
}
