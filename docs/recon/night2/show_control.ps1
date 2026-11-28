# Prints what a scenario's run says of its get-up, its behaviour and its commands (control.get_up, control.behaviour, control.commands)
# and its states, for a few scenarios at once.
#   powershell -ExecutionPolicy Bypass -File docs\recon\night2\show_control.ps1 -Names "a,b" [-Root Saved\Scenarios]
param([Parameter(Mandatory)][string]$Names, [string]$Root = '')
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
if (-not $Root) { $Root = Join-Path $repo 'Saved\Scenarios' }
foreach ($name in $Names.Split(',')) {
    $file = Join-Path $Root "$($name.Trim())\metrics.json"
    if (-not (Test-Path $file)) { Write-Output "$name : no metrics"; continue }
    $m = Get-Content $file -Raw | ConvertFrom-Json
    Write-Output "=== $($m.scenario) pass=$($m.pass)"
    $c = $m.control
    $g = $c.get_up
    $starts = @($g.starts | ForEach-Object { '{0:N2}' -f $_.at_s }) -join ' '
    Write-Output ("  states: " + (@($c.states | ForEach-Object { if ($_ -is [string]) { $_ } else { "$($_.state)@$('{0:N2}' -f $_.at_s)" } }) -join ' > '))
    Write-Output "  get_up: attempts=$($g.attempts) stood=$($g.stood) starts=[$starts] hits=$($g.hits) given_up_by=$($g.given_up_by | ConvertTo-Json -Compress)"
    $b = $c.behaviour
    if ($b) {
        $covers = @($b.covers | ForEach-Object { "$($_.place)@$('{0:N2}' -f $_.at_s)" }) -join ' '
        $low = @($b.lowerings | ForEach-Object { "dec $('{0:N2}' -f $_.decided_s) $($_.ended)" }) -join '; '
        Write-Output "  behaviour: fear_max=$($b.fear_max) protect_s=$($b.protect_s) covers=[$covers] lowerings=[$low] begun_near_afraid=$($b.begun_near_afraid)"
    }
    if ($c.commands) { Write-Output ("  commands: " + ($c.commands | ConvertTo-Json -Compress -Depth 3)) }
}
