# The stand of falls and get-ups (Tools\getup-stand.ps1) on several seeds side by side, one line a seed: a setting
# is chosen on three seeds, never on one (a figure from one seed is luck).
#   powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1
#   ... -Seeds "1,2,3"          the seeds, each a stand of its own
#   ... -Tune "name=value,..."  tuning overrides for every run
#   ... -Label friction05       names the output: Saved\Matrix\getup_seeds\<Label>\seed_<n> (and seed_<n>.txt, what
#                               the stand printed)
#   ... -Parallel 3             engine processes of each stand; the stands run at the same time (three seeds at 3
#                               are nine processes: an engine process takes 1.75 GB, and fifteen ran out of 32 GB)
#   ... -PressN 300, -HoldHands 1, -Only "table,chair", -StopStandingS 0     passed on to every stand
#
# Prints, for each seed: the falls that end standing, those at the first attempt, attempts a fall, and the
# assist's mean share of body weight while getting up; then the sums. Exit code: 0 every stand met its criterion,
# 1 one did not, 2 a stand gave no result.
param(
    [string]$Seeds = '1,2,3',
    [string]$Tune,
    [string]$TuningDir,
    [string]$Label = 'seeds',
    [int]$Parallel = 3,
    [double]$PressN = 0,
    [int]$HoldHands = 0,
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
    # Without a trailing backslash, which would escape the closing quote of the argument.
    if ($TuningDir) { $arguments += " -TuningDir `"$([System.IO.Path]::GetFullPath($TuningDir).TrimEnd('\'))`"" }
    if ($PressN -gt 0) { $arguments += " -PressN $PressN" }
    if ($HoldHands -gt 0) { $arguments += " -HoldHands $HoldHands" }
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
$allFalls = @()
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
    if ($HoldHands -gt 0) {
        $line = Get-Content $one.printed | Where-Object { $_ -match 'pulled free and stood' } | Select-Object -First 1
        if ($line) { $pressed += '; ' + $line.Trim() }
    }
    $line = Get-Content $one.printed | Where-Object { $_ -match 'from the first attempt to the first standing' } | Select-Object -First 1
    if ($line) { $pressed += '; ' + $line.Trim() }
    # The authority by direction and by body (control.authority), as the stand printed it.
    $line = Get-Content $one.printed | Where-Object { $_ -match 'authority while getting up' } | Select-Object -First 1
    if ($line) { $pressed += '; ' + $line.Trim() }
    $line = Get-Content $one.printed | Where-Object { $_ -match 'were not led' } | Select-Object -First 1
    if ($line) { $pressed += '; ' + $line.Trim() }
    $lines += ('  seed {0}: {1} of {2} stand, {3} at the first attempt, {4:N2} attempts a fall, assist share {5:N2}{6}' -f `
        $one.seed, $up.Count, $falls.Count, $first, ($attempts / [Math]::Max($falls.Count, 1)), $share, $pressed)
    $sumStand += $up.Count; $sumFalls += $falls.Count; $sumFirst += $first; $sumAttempts += $attempts; $shares += $share
    $allFalls += $falls
    if (-not $result.pass -and $exit -eq 0) { $exit = 1 }
}
# R2's gate over every seed (docs/active-ragdoll.md, decisions 16 and 21): C19, attempts given up and the longest pose held while
# getting up; C27, the trunk from upright and the farther foot from the pelvis as the pelvis passes 70 cm, the median of all and of
# each clip. Read, not judged.
# The median as the scorecard takes it (Tools\natural.ps1): the mean of the two middle values of an even count.
$median = {
    param($values)
    $sorted = @($values | Sort-Object)
    if ($sorted.Count -eq 0) { return [double]::NaN }
    $middle = [int][Math]::Floor(($sorted.Count - 1) / 2)
    if ($sorted.Count % 2 -eq 1) { return [double]$sorted[$middle] }
    return ([double]$sorted[$middle] + [double]$sorted[$middle + 1]) / 2.0
}
$gateLine = $null
if ($allFalls.Count -gt 0) {
    $givenUp = 0
    foreach ($fall in $allFalls) { $givenUp += [Math]::Max([int]$fall.attempts - [int]$fall.successes, 0) }
    $holds = @($allFalls | Where-Object { $null -ne $_.held_pose_s } | ForEach-Object { [double]$_.held_pose_s })
    $longHolds = @($holds | Where-Object { $_ -gt 1.0 }).Count
    # No fall that got up has no pose held to tell of: said as not measured, not as 0.
    $longest = [double]::NaN
    if ($holds.Count -gt 0) { $longest = ($holds | Measure-Object -Maximum).Maximum }
    $shaped = @($allFalls | Where-Object { $null -ne $_.rise_trunk_deg -and [double]$_.rise_trunk_deg -ge 0 -and $null -ne $_.rise_feet_cm -and [double]$_.rise_feet_cm -ge 0 })
    $within = @($shaped | Where-Object { [double]$_.rise_trunk_deg -le 45 -and [double]$_.rise_feet_cm -le 30 }).Count
    $byClip = @()
    foreach ($group in @($shaped | Group-Object clip | Sort-Object Name)) {
        $byClip += ('{0} {1:N0} deg {2:N0} cm ({3} of {4})' -f ([string]$group.Name).Split('_')[-1], (& $median @($group.Group | ForEach-Object { [double]$_.rise_trunk_deg })),
            (& $median @($group.Group | ForEach-Object { [double]$_.rise_feet_cm })),
            @($group.Group | Where-Object { [double]$_.rise_trunk_deg -le 45 -and [double]$_.rise_feet_cm -le 30 }).Count, $group.Count)
    }
    $gateLine = ('  R2''s gate: C19 {0} of {1} attempts given up ({2:N1}%), a pose held over 1.0 s in {3} (longest {4:N2} s); C27 trunk {5:N1} deg, farther foot {6:N1} cm in the median, both within 45 deg and 30 cm in {7} of {8}; by clip: {9}' -f `
        $givenUp, $sumAttempts, (100.0 * $givenUp / [Math]::Max($sumAttempts, 1)), $longHolds, $longest,
        (& $median @($shaped | ForEach-Object { [double]$_.rise_trunk_deg })), (& $median @($shaped | ForEach-Object { [double]$_.rise_feet_cm })),
        $within, $shaped.Count, ($byClip -join ', '))
}
$verdict = 'OK'
if ($exit -ne 0) { $verdict = 'FAILED' }
$meanShare = 0.0
if ($shares.Count -gt 0) { $meanShare = ($shares | Measure-Object -Average).Average }
Write-Output ('GETUP SEEDS {0}: {1} of {2} fall(s) end standing, {3} at the first attempt, {4:N2} attempts a fall, assist share {5:N2}; {6} s; {7}' -f `
    $verdict, $sumStand, $sumFalls, $sumFirst, ($sumAttempts / [Math]::Max($sumFalls, 1)), $meanShare, $seconds, $Label)
if ($gateLine) { Write-Output $gateLine }
$lines | ForEach-Object { Write-Output $_ }
if ($Tune) { Write-Output "  tune: $Tune" }
if ($TuningDir) { Write-Output "  tuning directory: $TuningDir" }
exit $exit
