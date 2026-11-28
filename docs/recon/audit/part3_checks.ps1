# The checks after part 3 of the audit (docs/recon/audit_plan.md), one after another; each tool's printout in Saved\Audit\<step>.txt
# and its first lines here. Run from the repo root:
#   powershell -ExecutionPolicy Bypass -File docs\recon\audit\part3_checks.ps1
$ErrorActionPreference = 'Continue'
$out = 'Saved\Audit'
New-Item -ItemType Directory -Force $out | Out-Null
function Step([string]$Name, [string]$Script, [string[]]$Arguments, [string]$Pattern = '') {
    $started = Get-Date
    $file = Join-Path $out "$Name.txt"
    & powershell -NoProfile -ExecutionPolicy Bypass -File "Tools\$Script" @Arguments *> $file
    $code = $LASTEXITCODE
    $lines = @(Get-Content $file)
    $shown = @($lines | Select-Object -First 1)
    if ($Pattern) { $shown = @($lines | Where-Object { $_ -match $Pattern }) }
    Write-Output ("=== {0} (exit {1}, {2:N0} s, {3})" -f $Name, $code, ((Get-Date) - $started).TotalSeconds, (Get-Date -Format 'HH:mm:ss'))
    $shown | ForEach-Object { Write-Output "  $_" }
}
Step 'rebuild' 'build.ps1' @('-Rebuild')
Step 'test' 'test.ps1' @()
Step 'suite' 'suite.ps1' @('-Baseline', 'audit_before') '^(SUITE|BASELINE|SCENARIO FAIL)'
Step 'bench_hashes' 'bench.ps1' @('-Hashes', '-Label', 'audit_after', '-Against', 'audit_before') '^(BENCH|  \S)'
Step 'checks' 'checks.ps1' @('-What', 'suite,matrix,knockdowns,stand,seeds,natural_quick', '-Label', 'audit_after') '^(CHECKS|  \S)'
Step 'record_replay_perception' 'record-replay.ps1' @('-Perception')
Step 'record_replay_takes' 'record-replay.ps1' @('-Takes')
Step 'record_replay_walk' 'record-replay.ps1' @('-Walk', '-Twice')
Step 'play_smoke' 'play.ps1' @('-Smoke')
Step 'links' 'check-links.ps1' @()
