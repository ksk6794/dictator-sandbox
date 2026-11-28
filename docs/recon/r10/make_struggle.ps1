# R10, step 7 (behaviour.struggle; C35): writes the scenarios of a body that lies on its back and is dragged by two hands, by the wrists
# toward its head or by the ankles toward its feet, 2.2 m at about 0.8 m/s, with the struggle on (switched on by the scenario) and off.
# With -Seeds "1,2,3" it writes seeded copies into -Out instead (for the measure on three seeds: the chance and the vigours are drawn).
#   powershell -ExecutionPolicy Bypass -File docs\recon\r10\make_struggle.ps1
#   powershell -ExecutionPolicy Bypass -File docs\recon\r10\make_struggle.ps1 -Seeds "1,2,3,4,5,6" -Out Saved\Recon\night2\struggle_seeds
param([string]$Seeds = '', [string]$Out = '')
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$utf8 = New-Object Text.UTF8Encoding($false)

function New-Drag([string]$Name, [string]$By, [bool]$Struggle, [int]$Seed) {
    # By the wrists: the player's hands come from above each wrist over 0.4 s, take hold, and drag 220 cm toward the head (-y) and 30 cm
    # up over 2.8 s; by the ankles: 200 cm toward the feet (+y), along the floor. They let go at 4.2 s and go up.
    $bones = if ($By -eq 'wrists') { @('hand_l', 'hand_r') } else { @('foot_l', 'foot_r') }
    $starts = if ($By -eq 'wrists') { @(@(40, 25, 80), @(-40, 25, 80)) } else { @(@(40, 175, 60), @(-40, 175, 60)) }
    $to = if ($By -eq 'wrists') { @(0, -220, 30) } else { @(0, 200, 2) }
    $events = [System.Collections.ArrayList]@()
    if ($Struggle) { $events.Add([ordered]@{ t = 0.0; type = 'console'; command = 'Sandbox.Tune behaviour.struggle=1' }) | Out-Null }
    $hands = @('hand_left', 'hand_right')
    for ($i = 0; $i -lt 2; $i++) {
        # The hands come over 0.8 s (come in 0.4 s from 70 cm, they struck the ankles with 40 N*s: the leg was covered for 3 s).
        $events.Add([ordered]@{ t = 1.0; type = 'effector'; effector = $hands[$i]; at_bone = $bones[$i]; follow_bone_s = 0.9; path = @(
            [ordered]@{ t = 0.0; location_cm = $starts[$i]; grip = 0 },
            [ordered]@{ t = 0.8; location_cm = @(0, 0, 5); grip = 0 },
            [ordered]@{ t = 0.9; location_cm = @(0, 0, 3); grip = 0 },
            [ordered]@{ t = 0.95; location_cm = @(0, 0, 3); grip = 1 },
            [ordered]@{ t = 1.0; location_cm = @(0, 0, 3); grip = 1 },
            [ordered]@{ t = 3.8; location_cm = $to; grip = 1 },
            [ordered]@{ t = 3.9; location_cm = $to; grip = 0 },
            [ordered]@{ t = 4.4; location_cm = @($to[0], $to[1], ($to[2] + 100)); grip = 0 }) }) | Out-Null
    }
    $spec = [ordered]@{
        name = $Name
        duration_s = 15.0
        body = [ordered]@{
            mesh = '/Game/MetaHumans/SandboxNPC/Body/SKM_SandboxNPC_BodyMesh.SKM_SandboxNPC_BodyMesh'
            physics_asset = '/Game/Generated/Physics/PA_SandboxNPC.PA_SandboxNPC'
            attached_meshes = @('/Game/MetaHumans/SandboxNPC/Face/SKM_SandboxNPC_FaceMesh.SKM_SandboxNPC_FaceMesh')
            control = 'active'
            get_up = $true
            location_cm = @(0, 90, 0)
            laid_deg = 0
        }
        events = $events
        expect = [ordered]@{ tuning_changes = $(if ($Struggle) { 1 } else { 0 }); max_joint_stretch_cm = 3.0; measures = [ordered]@{} }
    }
    if ($Seed -gt 0) { $spec.seed = $Seed }
    return $spec
}

function Save($Spec, [string]$Dir) {
    $path = Join-Path $Dir "$($Spec.name).json"
    [IO.File]::WriteAllText($path, ($Spec | ConvertTo-Json -Depth 12), $utf8)
    Write-Output "written $path"
}

if ($Seeds) {
    $dir = Join-Path $repo $Out
    New-Item -ItemType Directory -Force $dir | Out-Null
    foreach ($seed in $Seeds.Split(',')) {
        foreach ($by in @('wrists', 'ankles')) {
            Save (New-Drag "drag_lying_${by}_seed$seed" $by $true ([int]$seed)) $dir
        }
    }
    return
}

$dir = Join-Path $repo 'Scenarios'
# With the struggle (no seed: it struggles, every attempt at full vigour): it is felt as a drag, it struggles, and stands in the end.
foreach ($by in @('wrists', 'ankles')) {
    $s = New-Drag "drag_lying_${by}_struggles" $by $true 0
    $s.expect.effector_grabs = 2
    $s.expect.end_state = 'standing'
    $s.expect.measures['control.behaviour.struggles.0.struggles'] = [ordered]@{ equals = $true }
    $s.expect.measures['control.behaviour.struggles.0.attempts'] = [ordered]@{ min = 1 }
    $s.expect.measures['control.pose.target_step_max_deg'] = [ordered]@{ max = 110 }
    # C25's bound (decision 15): the struggle's muscles come in over struggle_blend_s (at 0.15 s the commands jumped by 476 N*m).
    $s.expect.measures['control.pose.command_jump_max_nm'] = [ordered]@{ max = 439 }
    Save $s $dir
}
# C26 (FR-18) in the struggle's phase (R10, step 9): while it is dragged by the wrists and struggles, a blow of 40 N*s on the chest, a
# push of 30 N*s over 0.25 s, each at 3.2 s; and a hold: dragged by one wrist (the right hand), its left ankle taken by the other hand
# from 2.4 s for 1.2 s. Each felt, and it stands in the end.
foreach ($kind in @('hit', 'push')) {
    $s = New-Drag "struggle_phase_$kind" 'wrists' $true 0
    $s.name = "struggle_phase_$kind"
    if ($kind -eq 'hit') {
        $s.events.Add([ordered]@{ t = 3.2; type = 'impulse'; body = 'spine_04'; impulse_ns = 40; direction = @(0, 0, -1) }) | Out-Null
    } else {
        $s.events.Add([ordered]@{ t = 3.2; type = 'impulse'; body = 'spine_04'; impulse_ns = 30; direction = @(1, 0, 0); duration_s = 0.25 }) | Out-Null
    }
    $s.expect.end_state = 'standing'
    $s.expect.measures['control.behaviour.fear_hits'] = [ordered]@{ min = 2 }
    $s.expect.measures['control.behaviour.struggles.0.attempts'] = [ordered]@{ min = 1 }
    $s.expect.measures['control.pose.target_step_max_deg'] = [ordered]@{ max = 110 }
    $s.expect.measures['control.pose.command_jump_max_nm'] = [ordered]@{ max = 439 }
    Save $s $dir
}
$s = New-Drag 'struggle_phase_hold' 'wrists' $true 0
# The left hand takes the left ankle instead of the left wrist: one hand drags (the right wrist), the other holds a leg.
$s.events[1] = [ordered]@{ t = 2.4; type = 'effector'; effector = 'hand_left'; at_bone = 'foot_l'; follow_bone_s = 1.2; path = @(
    [ordered]@{ t = 0.0; location_cm = @(40, 175, 60); grip = 0 },
    [ordered]@{ t = 0.8; location_cm = @(0, 0, 5); grip = 0 },
    [ordered]@{ t = 0.9; location_cm = @(0, 0, 3); grip = 0 },
    [ordered]@{ t = 0.95; location_cm = @(0, 0, 3); grip = 1 },
    [ordered]@{ t = 2.1; location_cm = @(0, 0, 3); grip = 1 },
    [ordered]@{ t = 2.2; location_cm = @(0, 0, 3); grip = 0 },
    [ordered]@{ t = 2.7; location_cm = @(0, 0, 80); grip = 0 }) }
$s.expect.end_state = 'standing'
$s.expect.effector_grabs = 2
$s.expect.measures['control.behaviour.struggles.0.attempts'] = [ordered]@{ min = 1 }
$s.expect.measures['control.pose.target_step_max_deg'] = [ordered]@{ max = 110 }
$s.expect.measures['control.pose.command_jump_max_nm'] = [ordered]@{ max = 439 }
Save $s $dir

# The same drags with the struggle at 0 (the files): nothing struggles; the grips hold through the drag.
foreach ($by in @('wrists', 'ankles')) {
    $s = New-Drag "drag_lying_${by}_still" $by $false 0
    $s.expect.effector_grabs = 2
    $s.expect.end_state = 'standing'
    Save $s $dir
}
