# The figures of docs/fast-checks.md, section 2 ("How often and what runs"), from the table runs.ps1 writes.
#   powershell -ExecutionPolicy Bypass -File docs\recon\fast-checks\runs_check.ps1 -Csv <runs.csv>
param([Parameter(Mandatory = $true)][string]$Csv)
function Get-Median($values) { $sorted = @($values | Sort-Object); if ($sorted.Count -eq 0) { return $null }; return $sorted[[int][Math]::Floor($sorted.Count / 2)] }
$rows = @(Import-Csv $Csv | ForEach-Object {
    [pscustomobject]@{ session = $_.session; at = [datetime]$_.at; bg = ($_.bg -eq 'True'); seconds = [double]$_.seconds; build = ($_.build -eq 'True'); tools = $_.tools; own = $_.own; loop = ($_.loop -eq 'True'); wait = ($_.wait -eq 'True') }
})
"calls: $($rows.Count); sessions that started the engine: $(@($rows | Select-Object -ExpandProperty session -Unique).Count)"
"with a build: $(@($rows | Where-Object build).Count); without one: $(@($rows | Where-Object { -not $_.build -and -not $_.wait }).Count); in the background: $(@($rows | Where-Object bg).Count)"
$fg = @($rows | Where-Object { -not $_.bg })
'foreground: {0} calls, {1:N1} min of waiting, {2:N1} s a call on average' -f $fg.Count, (($fg | Measure-Object seconds -Sum).Sum / 60), (($fg | Measure-Object seconds -Average).Average)
$withRun = @($rows | Where-Object { $_.build -and ($_.tools -or $_.own) })
"builds that also ran the engine in the same call: $($withRun.Count); " + (($withRun | Group-Object tools | Sort-Object Count -Descending | Select-Object -First 6 | ForEach-Object { "$($_.Name)=$($_.Count)" }) -join ' ')
# From the end of a call that only built to the start of the session's next call, when that one runs without building.
$gaps = @()
foreach ($group in ($rows | Group-Object session)) {
    $list = @($group.Group | Sort-Object at)
    for ($i = 0; $i -lt $list.Count - 1; $i++) {
        if (-not $list[$i].build -or $list[$i].tools -or $list[$i].own -or $list[$i + 1].build) { continue }
        $gaps += ($list[$i + 1].at - $list[$i].at.AddSeconds($list[$i].seconds)).TotalSeconds
    }
}
'after a call that only built, the next run came after: {0} cases, median {1:N0} s, mean {2:N0} s' -f $gaps.Count, (Get-Median $gaps), (($gaps | Measure-Object -Average).Average)
$pauses = @()
foreach ($group in ($rows | Group-Object session)) { $list = @($group.Group | Sort-Object at); for ($i = 1; $i -lt $list.Count; $i++) { $pauses += ($list[$i].at - $list[$i - 1].at).TotalSeconds } }
'median pause between the starts of two calls: {0:N0} s' -f (Get-Median $pauses)
