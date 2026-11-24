# Measures the friction between the NPC's body and the floor by experiment, not from the settings: the body is
# knocked down, lies still, and is then dragged over the floor by a steady force on its pelvis. While everything
# that carries it slides, the floor takes mu * m * g of that force, so from the trace (the mass-weighted mean of
# the bodies' places, every 0.1 s):
#   while it is pushed   mu = (F / m - a) / g        after the push   mu = -a / g
#   powershell -ExecutionPolicy Bypass -File Tools\surface-friction.ps1
#   ... -ForceN 800 -PushS 1.0   the push. A force below mu * m * g (549 N at 0.7) only pulls the body straight
#   ... -Tune "surface.friction=0.3,surface.feet_friction=0.3"    tuning overrides for the run
#   ... -Out <dir>               output directory (default Saved\Matrix\surface_friction)
#
# The floor of a scenario is 20 m by 20 m: a long push at low friction takes the body over its edge, which is an
# error here. At high friction (0.9 and more) limbs catch and roll, and the figure reads a little low.
# Prints one line, writes <Out>\surface-friction.json. Exit code: 0 measured, 2 the run gave no result or the body
# did not slide.
param(
    [double]$ForceN = 800,
    [double]$PushS = 1.0,
    [string]$Tune,
    [string]$Out
)
. "$PSScriptRoot\common.ps1"

if (-not $Out) { $Out = Join-Path $RepoRoot 'Saved\Matrix\surface_friction' }
$Out = [System.IO.Path]::GetFullPath($Out)
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
New-Item -ItemType Directory -Force $Out | Out-Null

$startS = 7.0
$name = 'surface_friction_drag'
$scenario = [ordered]@{
    name                  = $name
    duration_s            = 14.0
    checkpoint_interval_s = 0.1
    body                  = [ordered]@{
        mesh            = '/Game/MetaHumans/SandboxNPC/Body/SKM_SandboxNPC_BodyMesh.SKM_SandboxNPC_BodyMesh'
        physics_asset   = '/Game/Generated/Physics/PA_SandboxNPC.PA_SandboxNPC'
        attached_meshes = @('/Game/MetaHumans/SandboxNPC/Face/SKM_SandboxNPC_FaceMesh.SKM_SandboxNPC_FaceMesh')
        location_cm     = @(0, 0, 0)
        control         = 'active'
    }
    events                = @(
        [ordered]@{ t = 1.0; type = 'impulse'; body = 'spine_04'; impulse_ns = 200; direction = @(0, -1, 0) },
        [ordered]@{ t = $startS; type = 'impulse'; body = 'pelvis'; impulse_ns = ($ForceN * $PushS); duration_s = $PushS; direction = @(0, 1, 0) }
    )
    expect                = [ordered]@{ fell = $true }
}
$file = Join-Path $Out "$name.json"
$scenario | ConvertTo-Json -Depth 6 | Set-Content -Path $file -Encoding ascii

$arguments = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'scenario.ps1'), '-File', $file)
if ($Tune) { $arguments += @('-Tune', $Tune) }
$printed = & powershell @arguments
$runDir = Join-Path $RepoRoot "Saved\Scenarios\$name"
$run = Read-JsonFile (Join-Path $runDir 'metrics.json')
$trace = Read-JsonFile (Join-Path $runDir 'trace.json')
if (-not $run -or -not $run.ok -or -not $trace) {
    Write-Output "SURFACE FRICTION ERROR: the run gave no result ($($printed | Select-Object -First 1))"
    exit 2
}

$massOf = @{}
foreach ($one in $run.body.bodies) { $massOf[$one.bone] = [double]$one.mass_kg }
$totalKg = [double]$run.body.total_mass_kg
$places = @()
foreach ($checkpoint in $trace.checkpoints) {
    if ($checkpoint.frame -lt 0) { continue }
    $along = 0.0; $height = 0.0
    foreach ($traced in $checkpoint.bodies) {
        if ($massOf.ContainsKey($traced.bone)) {
            $along += $massOf[$traced.bone] * [double]$traced.p[1]
            $height += $massOf[$traced.bone] * [double]$traced.p[2]
        }
    }
    $places += [pscustomobject]@{ t = [double]$checkpoint.t; y = $along / $totalKg / 100.0; z = $height / $totalKg / 100.0 }
}
$speeds = @()
for ($i = 1; $i -lt $places.Count; $i++) {
    $step = $places[$i].t - $places[$i - 1].t
    if ($step -gt 0) { $speeds += [pscustomobject]@{ t = ($places[$i].t + $places[$i - 1].t) / 2; v = ($places[$i].y - $places[$i - 1].y) / $step } }
}
function Get-Slope($rows) {
    if (@($rows).Count -lt 3) { return $null }
    $meanT = ($rows | Measure-Object t -Average).Average
    $meanV = ($rows | Measure-Object v -Average).Average
    $above = 0.0; $below = 0.0
    foreach ($row in $rows) { $above += ($row.t - $meanT) * ($row.v - $meanV); $below += ($row.t - $meanT) * ($row.t - $meanT) }
    return $above / $below
}
$endS = $startS + $PushS
$peak = ($speeds | Where-Object { $_.t -ge $startS } | Measure-Object v -Maximum).Maximum
$slid = ($places | Select-Object -Last 1).y - ($places | Where-Object { $_.t -le $startS } | Select-Object -Last 1).y
$lowest = ($places | Measure-Object z -Minimum).Minimum
if ($lowest -lt -0.5) {
    Write-Output "SURFACE FRICTION ERROR: the body went over the floor's edge; give a shorter or a weaker push"
    exit 2
}
# The first 0.3 s of the push pull the body straight; the last of the coasting is limbs coming to rest one by one.
$pushed = @($speeds | Where-Object { $_.t -ge $startS + 0.3 -and $_.t -le $endS - 0.05 })
$coasting = @($speeds | Where-Object { $_.t -ge $endS + 0.05 -and $_.v -gt 0.25 * $peak })
$whilePushed = Get-Slope $pushed
$after = Get-Slope $coasting
$g = 9.8
$muPushed = $null; $muCoasting = $null
if ($null -ne $whilePushed) { $muPushed = [Math]::Round(($ForceN / $totalKg - $whilePushed) / $g, 3) }
if ($null -ne $after) { $muCoasting = [Math]::Round(-$after / $g, 3) }
$result = [ordered]@{
    ok              = $true
    force_n         = $ForceN
    push_s          = $PushS
    tune            = "$Tune"
    mass_kg         = $totalKg
    peak_speed_mps  = [Math]::Round($peak, 3)
    slid_m          = [Math]::Round($slid, 3)
    friction_pushed = $muPushed
    friction_coasting = $muCoasting
    surfaces        = $run.body.surfaces
    hash            = $run.hashes.bodies
}
$result | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $Out 'surface-friction.json') -Encoding ascii
if ($peak -lt 0.5 -or $null -eq $muPushed) {
    Write-Output ('SURFACE FRICTION: the body did not slide under {0:N0} N (it moved {1:N2} m): the friction is {2:N2} or more' -f $ForceN, $slid, ($ForceN / ($totalKg * $g)))
    exit 2
}
$coastText = 'too short a slide to tell'
if ($null -ne $muCoasting) { $coastText = '{0:N2}' -f $muCoasting }
Write-Output ('SURFACE FRICTION: {0:N2} while pushed, {1} after the push ({2:N0} N on the pelvis for {3:N1} s; {4:N1} kg, {5:N2} m/s at most, slid {6:N2} m)' -f `
    $muPushed, $coastText, $ForceN, $PushS, $totalKg, $peak, $slid)
exit 0
