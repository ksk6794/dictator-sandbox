# The stand of falls and get-ups (Tools\getup-stand.ps1) on several seeds side by side, one line a seed: a setting
# is chosen on three seeds, never on one (a figure from one seed is luck).
#   powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1
#   ... -Seeds "1,2,3"          the seeds, each a stand of its own
#   ... -Tune "name=value,..."  tuning overrides for every run
#   ... -Label friction05       names the output: Saved\Matrix\getup_seeds\<Label>\seed_<n> (and seed_<n>.txt, what
#                               the stand printed)
#   ... -Parallel 5             engine processes of each stand; the stands run at the same time
#   ... -PressN 300, -Only "table,chair", -StopStandingS 0     passed on to every stand
#
# Prints, for each seed: the falls that end standing, those at the first attempt, attempts a fall, and the
# assist's mean share of body weight while getting up; then the sums. Exit code: 0 every stand met its criterion,
# 1 one did not, 2 a stand gave no result.
param(
    [string]$Seeds = '1,2,3',
    [string]$Tune,
    [string]$Label = 'seeds',
    [int]$Parallel = 5,
    [double]$PressN = 0,
    [string]$Only,
    [double]$StopStandingS = -1
)
. "$PSScriptRoot\common.ps1"

$root = Join-Path $RepoRoot "Saved\Matrix\getup_seeds\$Label"
if (Test-Path $root) { Remove-Item $root -Recurse -Force }
New-Item -ItemType Directory -Force $root | Out-Null
$stand = Join-Path $PSScriptRoot 'getup-stand.ps1'
$started = Get-Date

$running = @()
foreach ($seedText in ($Seeds -split ',')) {
    $seedNumber = [int]$seedText.Trim()
    $outDir = Join-Path $root "seed_$seedNumber"
    $arguments = "-ExecutionPolicy Bypass -File `"$stand`" -Seed $seedNumber -Parallel $Parallel -Out `"$outDir`""
    if ($Tune) { $arguments += " -Tune `"$Tune`"" }
    if ($PressN -gt 0) { $arguments += " -PressN $PressN" }
    if ($Only) { $arguments += " -Only `"$Only`"" }
    if ($StopStandingS -ge 0) { $arguments += " -StopStandingS $StopStandingS" }
    $printed = Join-Path $root "seed_$seedNumber.txt"
    $process = Start-Process powershell -ArgumentList $arguments -RedirectStandardOutput $printed -WindowStyle Hidden -PassThru
    $running += [pscustomobject]@{ seed = $seedNumber; process = $process; dir = $outDir; printed = $printed }
}
foreach ($one in $running) { $one.process.WaitForExit() }
$seconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 0)

$exit = 0
$sumStand = 0; $sumFalls = 0; $sumFirst = 0; $sumAttempts = 0; $shares = @()
$lines = @()
foreach ($one in $running) {
    $result = Read-JsonFile (Join-Path $one.dir 'getup-stand.json')
    if (-not $result -or -not $result.ok) {
        $lines += ('  seed {0}: no result (see {1})' -f $one.seed, $one.printed)
        $exit = 2
        continue
    }
    $falls = @($result.results | Where-Object { $_.fell })
    $up = @($falls | Where-Object { $_.got_up })
    $first = @($up | Where-Object { $_.attempts -eq 1 }).Count
    $attempts = ($falls | Measure-Object attempts -Sum).Sum
    $share = ($falls | Where-Object { $_.assist_share_mean -gt 0 } | Measure-Object assist_share_mean -Average).Average
    $pressed = ''
    if ($PressN -gt 0) {
        $line = Get-Content $one.printed | Where-Object { $_ -match 'stood while they were pressed' } | Select-Object -First 1
        if ($line) { $pressed = '; ' + $line.Trim() }
    }
    $lines += ('  seed {0}: {1} of {2} stand, {3} at the first attempt, {4:N2} attempts a fall, assist share {5:N2}{6}' -f `
        $one.seed, $up.Count, $falls.Count, $first, ($attempts / [Math]::Max($falls.Count, 1)), $share, $pressed)
    $sumStand += $up.Count; $sumFalls += $falls.Count; $sumFirst += $first; $sumAttempts += $attempts; $shares += $share
    if (-not $result.pass -and $exit -eq 0) { $exit = 1 }
}
$verdict = 'OK'
if ($exit -ne 0) { $verdict = 'FAILED' }
$meanShare = 0.0
if ($shares.Count -gt 0) { $meanShare = ($shares | Measure-Object -Average).Average }
Write-Output ('GETUP SEEDS {0}: {1} of {2} fall(s) end standing, {3} at the first attempt, {4:N2} attempts a fall, assist share {5:N2}; {6} s; {7}' -f `
    $verdict, $sumStand, $sumFalls, $sumFirst, ($sumAttempts / [Math]::Max($sumFalls, 1)), $meanShare, $seconds, $Label)
$lines | ForEach-Object { Write-Output $_ }
if ($Tune) { Write-Output "  tune: $Tune" }
exit $exit
