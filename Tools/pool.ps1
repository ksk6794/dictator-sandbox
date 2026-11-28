# A pool of engine processes that wait for jobs (docs/fast-checks.md, SP-4; docs/recon/sp4_plan.md): started once, they take the
# headless jobs of every tool that runs scenarios (Invoke-ScenarioBatch) until they have waited -IdleS seconds with nothing to do,
# so that a tool's call does not pay the engine's start. A pool started on one build or settings is not used on another (its
# fingerprint), and build.ps1, gen-assets.ps1, bake-lighting.ps1, bench.ps1 and every run in the headset stop it first.
#   powershell -ExecutionPolicy Bypass -File Tools\pool.ps1 -Start [-Processes 8] [-IdleS 600]
#   powershell -ExecutionPolicy Bypass -File Tools\pool.ps1 -Stop
#   powershell -ExecutionPolicy Bypass -File Tools\pool.ps1 -Status
param([switch]$Start, [switch]$Stop, [switch]$Status, [int]$Processes = 8, [int]$IdleS = 600)
. "$PSScriptRoot\common.ps1"

if ($Stop -or $Start) {
    $why = Stop-Pool
    if ($why) { Write-Output "POOL ERROR: not stopped: $why"; exit 1 }
    if ($Stop) { Write-Output 'POOL STOPPED'; exit 0 }
}
if ($Start) {
    $dir = Get-PoolDir
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue }
    New-Item -ItemType Directory -Force (Join-Path $dir 'taken') | Out-Null
    # A stop or a drain of the pool before must not end this one at its birth.
    foreach ($left in @('stop', 'draining')) {
        $path = Join-Path $dir $left
        if (Test-Path $path) { Remove-Item $path -Force -ErrorAction SilentlyContinue }
        if (Test-Path $path) { Write-Output "POOL ERROR: $path cannot be removed"; exit 1 }
    }
    $count = [Math]::Max(1, [Math]::Min($Processes, 9))
    $pool = [pscustomobject]@{ print = (Get-PoolPrint); idle_s = $IdleS; parallel = $count; started = 0; processes = @(); since = (Get-Date).ToString('s') }
    # The pool's file is there before its first process looks for it (a process that misses it exits) and names every process started.
    $lock = Lock-Pool
    try {
        Write-Pool $pool
        for ($i = 0; $i -lt $count; $i++) {
            $why = Test-EngineRoom -PendingGB (1.7 * $i) -NoYield
            if ($why) { [Console]::Error.WriteLine("POOL NOTE: $i process(es), no room for more ($why)"); break }
            [void](Start-PoolProcess $pool)
            Write-Pool $pool
        }
    } finally { $lock.Dispose() }
    Write-Output ('POOL STARTED: {0} process(es), waiting {1} s at most, fingerprint {2}' -f @($pool.processes).Count, $IdleS, $pool.print)
    exit 0
}
# -Status, and by default.
$pool = Read-Pool
if (-not $pool) { Write-Output 'POOL NONE'; exit 0 }
$alive = @(Get-PoolAlive $pool)
$current = Get-PoolPrint
$state = 'current'
if ($pool.print -ne $current) { $state = 'another build or settings' }
$dir = Get-PoolDir
$exited = @(Get-ChildItem $dir -Filter 'exited_*.txt' -File -ErrorAction SilentlyContinue | ForEach-Object { '{0}: {1}' -f ($_.BaseName -replace 'exited_', ''), ([System.IO.File]::ReadAllText($_.FullName).Trim()) })
$busy = @(Get-PoolOpenJobs).Count
if (Test-PoolEnding) { $state += ', ending' }
Write-Output ('POOL {0}: {1} of {2} process(es) alive, {3} with a job, fingerprint {4} ({5}), {6} job(s) waiting' -f $(if ($alive.Count -gt 0) { 'RUNNING' } else { 'ENDED' }), $alive.Count, $pool.started, $busy, $pool.print, $state, @(Get-ChildItem $dir -Filter '*.json' -File -ErrorAction SilentlyContinue).Count)
foreach ($line in $exited) { Write-Output "  exited $line" }
