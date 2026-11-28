# Stage 1 of docs/recon/window3_plan.md: getup.strict_release measured, off (the files) and on (-Tune), on the same tree.
# - the check set (suite, matrix, knockdowns, the stand, the seeds, the scorecard) with it on, under the label w3s1_on;
# - the three seeds with a viewer at 70 cm (C33), with get_up 3 s after it came (C39), pressed by 300 N and held by one hand,
#   each off and on.
#   powershell -ExecutionPolicy Bypass -File docs\recon\window3\step1_measure.ps1 [-Tune "getup.strict_release=1"] [-Tag on]
param([string]$Tune = 'getup.strict_release=1', [string]$Tag = 'on', [switch]$SkipOff, [switch]$SkipChecks, [string]$Only = 'near,command,press,hold1',
    [string]$What = 'suite,matrix,knockdowns,stand,seeds,natural_quick')
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$log = Join-Path $repo 'Saved\Recon\window3'
New-Item -ItemType Directory -Force $log | Out-Null
function Invoke-Logged([string]$Name, [string[]]$ToolArgs) {
    $file = Join-Path $log "$Name.txt"
    & powershell -ExecutionPolicy Bypass -File @ToolArgs *> $file
    $first = Get-Content $file | Where-Object { $_ -match '^(CHECKS|STAND|SEEDS|GETUP|SUITE|[A-Z]+ (OK|FAIL|ERROR))' } | Select-Object -First 3
    Write-Output ("{0}: {1}" -f $Name, ($first -join ' | '))
}
if (-not $SkipChecks) {
    Invoke-Logged "checks_$Tag" @('Tools\checks.ps1', '-What', $What, '-Tune', $Tune, '-Label', "w3s1_$Tag")
}
$sets = @(
    @{ Name = 'near'; Args = @('-Near', '70') },
    @{ Name = 'command'; Args = @('-Near', '70', '-CommandAtS', '3') },
    @{ Name = 'press'; Args = @('-PressN', '300') },
    @{ Name = 'hold1'; Args = @('-HoldHands', '1') }
)
$wanted = $Only -split ','
foreach ($set in @($sets | Where-Object { $wanted -contains $_.Name })) {
    if (-not $SkipOff) {
        Invoke-Logged "seeds_$($set.Name)_off" (@('Tools\getup-seeds.ps1', '-Label', "w3s1_$($set.Name)_off") + $set.Args)
    }
    Invoke-Logged "seeds_$($set.Name)_$Tag" (@('Tools\getup-seeds.ps1', '-Label', "w3s1_$($set.Name)_$Tag", '-Tune', $Tune) + $set.Args)
}
'STEP1 MEASURE DONE'
