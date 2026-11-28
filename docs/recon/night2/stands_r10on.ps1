# The stands of stage 1 (R10 on by decision 67), one after another (each is one getup-seeds call of nine processes; they differ
# in their arguments, so they cannot share a queue): a viewer near every fallen body; the same with get_up told 3 s after it came;
# 300 N pressing every fallen body down; one hand holding the chest of every fallen body.
#   powershell -ExecutionPolicy Bypass -File docs\recon\night2\stands_r10on.ps1 [-Tag r10on]
param([string]$Tag = 'r10on')
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$seeds = Join-Path $repo 'Tools\getup-seeds.ps1'
$runs = @(
    @{ label = "${Tag}_near"; args = @('-Near', '70') },
    @{ label = "${Tag}_command"; args = @('-Near', '70', '-CommandAtS', '3') },
    @{ label = "${Tag}_press"; args = @('-PressN', '300') },
    @{ label = "${Tag}_hold1"; args = @('-HoldHands', '1') }
)
foreach ($run in $runs) {
    $out = Join-Path $repo "Saved\night2_$($run.label).txt"
    & powershell -ExecutionPolicy Bypass -File $seeds -Label $run.label @($run.args) *> $out
    Write-Output ("{0}: {1}" -f $run.label, (Get-Content $out | Where-Object { $_ -match '^GETUP SEEDS' } | Select-Object -First 1))
}
