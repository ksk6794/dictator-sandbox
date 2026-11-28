# R10, step 5: writes the scenarios of staying down and lowering (C33) and their phases (C26) into Scenarios\.
# Since decision 67 (28 November) fear, stay_down, protect and commands are on in the files, and a scenario that wants one off
# switches it off itself. The body is laid on its back, tapped (a hit of 35 N*s into the floor, so that it is afraid: fear 0.7,
# above fear_stay_down 0.2 for 10.7 s; under protect_from_ns, 40, so that the tap is no blow into the groin that it covers) and gets
# up by itself unless the viewer is near. (Until decision 67 each switched its behaviours on itself and the tap was 40 N*s.)
#   powershell -ExecutionPolicy Bypass -File docs\recon\r10\make_protect.ps1
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$utf8 = New-Object Text.UTF8Encoding($false)

function New-Base([string]$Name, [double]$Duration) {
    [ordered]@{
        name = $Name
        duration_s = $Duration
        body = [ordered]@{
            mesh = '/Game/MetaHumans/SandboxNPC/Body/SKM_SandboxNPC_BodyMesh.SKM_SandboxNPC_BodyMesh'
            physics_asset = '/Game/Generated/Physics/PA_SandboxNPC.PA_SandboxNPC'
            attached_meshes = @('/Game/MetaHumans/SandboxNPC/Face/SKM_SandboxNPC_FaceMesh.SKM_SandboxNPC_FaceMesh')
            control = 'active'
            get_up = $true
            location_cm = @(0, 90, 0)
            laid_deg = 0
        }
        events = [System.Collections.ArrayList]@(
            [ordered]@{ t = 0.6; type = 'impulse'; body = 'pelvis'; impulse_ns = 35; direction = @(0, 0, -1) }
        )
        expect = [ordered]@{ tuning_changes = 0; max_joint_stretch_cm = 2.0; measures = [ordered]@{} }
    }
}

# The viewer waits 3 m to the side of where the body lies, comes to 70 cm of its pelvis over ArriveS from AtS, stays StayS, and goes.
function Add-Viewer($Spec, [double]$AtS, [double]$ArriveS, [double]$StayS) {
    $Spec.events.Add([ordered]@{
        t = $AtS; type = 'viewer'; at_bone = 'pelvis'
        path = @(
            [ordered]@{ t = 0.0; location_cm = @(300, 90, 165) },
            [ordered]@{ t = 0.05; location_cm = @(300, 0, 160) },
            [ordered]@{ t = 0.05 + $ArriveS; location_cm = @(70, 0, 160) },
            [ordered]@{ t = 0.05 + $ArriveS + $StayS; location_cm = @(70, 0, 160) },
            [ordered]@{ t = 1.05 + $ArriveS + $StayS; location_cm = @(400, 0, 160) }
        )
    }) | Out-Null
}

function Save($Spec) {
    $path = Join-Path $repo "Scenarios\$($Spec.name).json"
    [IO.File]::WriteAllText($path, ($Spec | ConvertTo-Json -Depth 12), $utf8)
    Write-Output "written $($Spec.name)"
}

# C33: it lies covered while the viewer stands near (no attempt in 10 s), and gets up once the viewer has gone.
$s = New-Base 'protect_near_lying' 22.0
Add-Viewer $s 0.9 1.0 9.0
$s.expect.end_state = 'standing'
$s.expect.min_get_ups = 1
$s.expect.max_get_up_attempts = 1
$s.expect.measures['control.behaviour.begun_near_afraid'] = [ordered]@{ max = 0 }
$s.expect.measures['control.behaviour.protect_s'] = [ordered]@{ min = 7.0 }
$s.expect.measures['control.get_up.starts.0.at_s'] = [ordered]@{ min = 11.0 }
$s.expect.measures['control.pose.target_step_max_deg'] = [ordered]@{ max = 110 }
Save $s

# C33: the viewer comes near as it sits up, kneels and rises: turned back within 0.5 s, lowered (the head under 2.0 m/s, the pelvis
# under 1.0), covered while the viewer stays, up once it has gone. ArriveAt is when the viewer is 70 cm from the pelvis.
# (The clip of getup_laid_back runs from 2.6 s, after the muscles' ease: sitting up 0.3 to 0.8 s into it, on the knees about 1.3, rising
# about 2.0.)
foreach ($phase in @(@('sit', 3.1), @('knees', 3.9), @('rise', 4.6))) {
    $s = New-Base "protect_near_rising_$($phase[0])" 24.0
    Add-Viewer $s ([double]$phase[1] - 0.55) 0.5 6.0
    $s.expect.end_state = 'standing'
    $s.expect.min_get_ups = 1
    $s.expect.min_get_up_attempts = 2
    $s.expect.measures['control.get_up.given_up_by.afraid'] = [ordered]@{ min = 1; max = 1 }
    $s.expect.measures['control.behaviour.turned_back_within_s'] = [ordered]@{ max = 0.5 }
    $s.expect.measures['control.behaviour.lowered_head_down_mps'] = [ordered]@{ max = 2.0 }
    $s.expect.measures['control.behaviour.lowered_pelvis_down_mps'] = [ordered]@{ max = 1.0 }
    $s.expect.measures['control.behaviour.begun_near_afraid'] = [ordered]@{ max = 0 }
    $s.expect.measures['control.pose.target_step_max_deg'] = [ordered]@{ max = 110 }
    Save $s
}

# Step 6 in part (behaviour.protect; C34): a hit of 15 N*s on the head while it lies (1.0 s, before its get-up at 2.0 s) and while it
# kneels (3.9 s): it covers its head within 0.6 s (one forearm within 10 cm, both within 15), holds it protect_hold_s, then gets up.
# No viewer and no fear: the cover is the hit's.
foreach ($case in @(@('lying', 1.0), @('rising', 3.9))) {
    $s = New-Base "protect_hit_$($case[0])_head" 20.0
    $s.events = [System.Collections.ArrayList]@(
        [ordered]@{ t = [double]$case[1]; type = 'impulse'; body = 'head'; impulse_ns = 15; direction = @(1, 0, 0) })
    $s.expect.end_state = 'standing'
    $s.expect.min_get_ups = 1
    $s.expect.measures['control.behaviour.covers.0.one_within_s'] = [ordered]@{ min = 0.0; max = 0.6 }
    $s.expect.measures['control.behaviour.covers.0.both_within_s'] = [ordered]@{ min = 0.0; max = 0.6 }
    $s.expect.measures['control.behaviour.covers.0.head_down_mps'] = [ordered]@{ max = 2.0 }
    $s.expect.measures['control.behaviour.protect_s'] = [ordered]@{ min = 2.5 }
    $s.expect.measures['control.pose.target_step_max_deg'] = [ordered]@{ max = 110 }
    if ($case[0] -eq 'rising') { $s.expect.measures['control.get_up.given_up_by.protect'] = [ordered]@{ min = 1; max = 1 } }
    Save $s
}

# Step 6, the other places (C34: one forearm within 10 cm of the place within 0.6 s): 50 N*s on the chest, into the groin (the pelvis from
# the front of a body on its back) and on the left thigh while it lies (1.0 s), and on the chest and the thigh as it kneels (3.9 s).
foreach ($case in @(@('lying_trunk', 1.0, 'spine_04', @(0, 0, -1), 'trunk'), @('lying_groin', 1.0, 'pelvis', @(0, 0, -1), 'groin'),
        @('lying_legs', 1.0, 'thigh_l', @(1, 0, 0), 'leg'), @('rising_trunk', 3.9, 'spine_04', @(1, 0, 0), 'trunk'),
        @('rising_legs', 3.9, 'thigh_l', @(1, 0, 0), 'leg'))) {
    $s = New-Base "protect_hit_$($case[0])" 20.0
    $s.events = [System.Collections.ArrayList]@(
        [ordered]@{ t = [double]$case[1]; type = 'impulse'; body = $case[2]; impulse_ns = 50; direction = $case[3] })
    $s.expect.end_state = 'standing'
    $s.expect.min_get_ups = 1
    $s.expect.measures['control.behaviour.covers.0.place'] = [ordered]@{ equals = $case[4] }
    # C34 as met today: not for a leg struck as the body kneels (it falls first: 1.07 s); held to what it does, so that a change is seen.
    if ($case[0] -eq 'rising_legs') { $s.expect.measures['control.behaviour.covers.0.one_within_s'] = [ordered]@{ min = 0.0; max = 1.2 } }
    else { $s.expect.measures['control.behaviour.covers.0.one_within_s'] = [ordered]@{ min = 0.0; max = 0.6 } }
    $s.expect.measures['control.behaviour.covers.0.head_down_mps'] = [ordered]@{ max = 2.0 }
    $s.expect.measures['control.behaviour.protect_s'] = [ordered]@{ min = 2.5 }
    $s.expect.measures['control.pose.target_step_max_deg'] = [ordered]@{ max = 110 }
    if ($case[0].StartsWith('rising')) { $s.expect.measures['control.get_up.given_up_by.protect'] = [ordered]@{ min = 1; max = 1 } }
    Save $s
}

# Step 8 in part (C39): get_up with the player near (up at once, done), get_up and then stop before it moves (it stays down), and the
# command refused with the switch off.
$s = New-Base 'command_get_up_near' 22.0
$s.events.Add([ordered]@{ t = 4.0; type = 'console'; command = 'Sandbox.Command get_up' }) | Out-Null
Add-Viewer $s 0.9 1.0 9.0
$s.expect.end_state = 'standing'
$s.expect.measures['control.commands.0.answer'] = [ordered]@{ equals = 'done' }
$s.expect.measures['control.commands.0.first_motion_after_s'] = [ordered]@{ min = 0.0; max = 1.0 }
$s.expect.measures['control.get_up.starts.0.at_s'] = [ordered]@{ min = 4.0; max = 5.0 }
$s.expect.measures['control.behaviour.begun_near_afraid'] = [ordered]@{ max = 0 }
Save $s
$s = New-Base 'command_stop' 22.0
$s.events.Add([ordered]@{ t = 4.0; type = 'console'; command = 'Sandbox.Command get_up' }) | Out-Null
$s.events.Add([ordered]@{ t = 4.05; type = 'console'; command = 'Sandbox.Command stop' }) | Out-Null
Add-Viewer $s 0.9 1.0 9.0
$s.expect.end_state = 'standing'
$s.expect.measures['control.commands.0.answer'] = [ordered]@{ equals = 'stopped' }
$s.expect.measures['control.commands.1.answer'] = [ordered]@{ equals = 'done' }
$s.expect.measures['control.get_up.starts.0.at_s'] = [ordered]@{ min = 11.0 }
Save $s
# get_up as an attempt is being lowered (the viewer came as it knelt): it turns up again over lower_turn_s and stands; and get_up with
# a hit on the head as it rises (protect on): the cover interrupts it once, and it goes on up (found by the independent reading of the
# commands: a lowering ended in one frame, a command left accepted after its attempt was given up).
$s = New-Base 'command_lowering_up' 22.0
Add-Viewer $s 3.35 0.5 8.0
$s.events.Add([ordered]@{ t = 4.4; type = 'console'; command = 'Sandbox.Command get_up' }) | Out-Null
$s.expect.end_state = 'standing'
$s.expect.measures['control.commands.0.answer'] = [ordered]@{ equals = 'done' }
$s.expect.measures['control.behaviour.lowerings.0.ended'] = [ordered]@{ equals = 'commanded up' }
$s.expect.measures['control.pose.command_jump_max_nm'] = [ordered]@{ max = 439 }
$s.expect.measures['control.pose.target_step_max_deg'] = [ordered]@{ max = 110 }
Save $s
$s = New-Base 'command_hit_interrupted' 24.0
Add-Viewer $s 0.9 1.0 12.0
$s.events.Add([ordered]@{ t = 4.0; type = 'console'; command = 'Sandbox.Command get_up' }) | Out-Null
$s.events.Add([ordered]@{ t = 6.0; type = 'impulse'; body = 'head'; impulse_ns = 15; direction = @(1, 0, 0) }) | Out-Null
$s.expect.end_state = 'standing'
$s.expect.measures['control.commands.0.answer'] = [ordered]@{ equals = 'done' }
$s.expect.measures['control.commands.0.interrupted'] = [ordered]@{ min = 1; max = 1 }
$s.expect.measures['control.get_up.given_up_by.protect'] = [ordered]@{ min = 1; max = 1 }
$s.expect.measures['control.behaviour.begun_near_afraid'] = [ordered]@{ max = 0 }
$s.expect.measures['control.pose.target_step_max_deg'] = [ordered]@{ max = 110 }
Save $s
# The plan's command_refused_held and command_get_up_held: the afraid body lies covered with the viewer near; one hand (or two) takes
# hold of a forearm at 5.4 s and holds 1.2 s; get_up is told at 6.0 s, while it is held.
function Add-HoldHand($Spec, [string]$Effector, [string]$Bone, [double]$AtS) {
    $Spec.events.Add([ordered]@{ t = $AtS; type = 'effector'; effector = $Effector; at_bone = $Bone; follow_bone_s = 1.2; path = @(
        [ordered]@{ t = 0.0; location_cm = @(40, 90, 120); grip = 0 },
        [ordered]@{ t = 0.5; location_cm = @(0, 0, 0); grip = 0 },
        [ordered]@{ t = 0.6; location_cm = @(0, 0, 0); grip = 1 },
        [ordered]@{ t = 1.8; location_cm = @(0, 0, 0); grip = 1 },
        [ordered]@{ t = 1.9; location_cm = @(0, 0, 0); grip = 0 },
        [ordered]@{ t = 2.4; location_cm = @(0, 0, 80); grip = 0 }) }) | Out-Null
}
foreach ($case in @(@('command_get_up_held', 1), @('command_refused_held', 2))) {
    $s = New-Base $case[0] 24.0
    Add-Viewer $s 0.9 1.0 9.0
    Add-HoldHand $s 'hand_right' 'lowerarm_l' 4.8
    if ($case[1] -eq 2) { Add-HoldHand $s 'hand_left' 'lowerarm_r' 4.8 }
    $s.events.Add([ordered]@{ t = 6.0; type = 'console'; command = 'Sandbox.Command get_up' }) | Out-Null
    $s.expect.end_state = 'standing'
    $s.expect.effector_grabs = $case[1]
    if ($case[1] -eq 2) {
        $s.expect.measures['control.commands.0.answer'] = [ordered]@{ equals = 'refused: held' }
        $s.expect.measures['control.get_up.starts.0.at_s'] = [ordered]@{ min = 11.0 }
    } else {
        $s.expect.measures['control.commands.0.answer'] = [ordered]@{ equals = 'done' }
    }
    $s.expect.measures['control.pose.target_step_max_deg'] = [ordered]@{ max = 110 }
    Save $s
}
# A body that has lain still for seconds, uncovered (a long rest: getup.rest_s 10), told to get up at 5.0 s: its reaction is timed from
# the command (C39: 0.2 to 1.0 s), not from when it came to rest (the third independent reading: it began 0.011 s after).
$s = New-Base 'command_get_up_still' 12.0
$s.events.Insert(0, [ordered]@{ t = 0.0; type = 'console'; command = 'Sandbox.Tune getup.rest_s=10' })
$s.events.Add([ordered]@{ t = 5.0; type = 'console'; command = 'Sandbox.Command get_up' }) | Out-Null
$s.expect.tuning_changes = 1
$s.expect.end_state = 'standing'
$s.expect.measures['control.commands.0.answer'] = [ordered]@{ equals = 'done' }
$s.expect.measures['control.commands.0.first_motion_after_s'] = [ordered]@{ min = 0.2; max = 1.0 }
Save $s
$s = New-Base 'command_refused_off' 6.0
$s.events = [System.Collections.ArrayList]@(
    [ordered]@{ t = 0.0; type = 'console'; command = 'Sandbox.Tune behaviour.commands=0' },
    [ordered]@{ t = 1.0; type = 'console'; command = 'Sandbox.Command get_up' })
$s.expect.tuning_changes = 1
$s.expect.measures['control.commands.0.answer'] = [ordered]@{ equals = 'refused: off' }
Save $s

# A switch changed while the body runs (docs/pitfalls.md: a value that "applied" has not necessarily reached what reads it): stay_down
# switched on as the body lies afraid, before the viewer comes (it then stays down and covers); protect switched on just before a hit on
# the chest of a lying body (it covers the trunk).
$s = New-Base 'tune_live_stay_down' 22.0
$s.events.Insert(0, [ordered]@{ t = 0.0; type = 'console'; command = 'Sandbox.Tune behaviour.stay_down=0' })
$s.events.Add([ordered]@{ t = 1.5; type = 'console'; command = 'Sandbox.Tune behaviour.stay_down=1' }) | Out-Null
Add-Viewer $s 0.9 1.0 9.0
$s.expect.tuning_changes = 2
$s.expect.end_state = 'standing'
$s.expect.measures['control.behaviour.begun_near_afraid'] = [ordered]@{ max = 0 }
$s.expect.measures['control.behaviour.protect_s'] = [ordered]@{ min = 7.0 }
$s.expect.measures['control.get_up.starts.0.at_s'] = [ordered]@{ min = 11.0 }
Save $s
$s = New-Base 'tune_live_protect' 20.0
$s.events = [System.Collections.ArrayList]@(
    [ordered]@{ t = 0.0; type = 'console'; command = 'Sandbox.Tune behaviour.protect=0' },
    [ordered]@{ t = 0.8; type = 'console'; command = 'Sandbox.Tune behaviour.protect=1' },
    [ordered]@{ t = 1.0; type = 'impulse'; body = 'spine_04'; impulse_ns = 50; direction = @(0, 0, -1) })
$s.expect.tuning_changes = 2
$s.expect.end_state = 'standing'
$s.expect.measures['control.behaviour.covers.0.place'] = [ordered]@{ equals = 'trunk' }
$s.expect.measures['control.behaviour.protect_s'] = [ordered]@{ min = 2.5 }
Save $s

# C26 (FR-18) in the two new phases: a blow (40 N*s), a push (30 N*s over 0.25 s) and a hold (a hand that follows the left forearm,
# takes hold and holds 1.2 s), while the body is lowered (the rise turned back at 4.58 s, lowered until about 7.0 s) and while it lies
# covered (protect_near_lying, covered from about 2 s to 11 s). Each asks that the body felt it and goes on: lowered or covered, then up.
$phases = @(
    @{ phase = 'lowering'; from = 'rise'; at = 5.6; duration = 24.0 },
    @{ phase = 'cover'; from = 'lying'; at = 6.0; duration = 22.0 }
)
foreach ($p in $phases) {
    foreach ($kind in @('hit', 'push', 'hold')) {
        $s = New-Base "protect_phase_$($p.phase)_$kind" $p.duration
        if ($p.from -eq 'rise') { Add-Viewer $s (4.6 - 0.55) 0.5 6.0 } else { Add-Viewer $s 0.9 1.0 9.0 }
        if ($kind -eq 'hit') {
            $s.events.Add([ordered]@{ t = $p.at; type = 'impulse'; body = 'spine_04'; impulse_ns = 40; direction = @(1, 0, 0) }) | Out-Null
        } elseif ($kind -eq 'push') {
            $s.events.Add([ordered]@{ t = $p.at; type = 'impulse'; body = 'spine_04'; impulse_ns = 30; direction = @(1, 0, 0); duration_s = 0.25 }) | Out-Null
        } else {
            # The hand waits above where the body lies and comes to the forearm over 0.5 s (a hand at 300 N does not cross a metre in 0.08 s).
            $s.events.Add([ordered]@{ t = $p.at - 0.6; type = 'effector'; effector = 'hand_right'; at_bone = 'lowerarm_l'; follow_bone_s = 1.2; path = @(
                [ordered]@{ t = 0.0; location_cm = @(40, 90, 120); grip = 0 },
                [ordered]@{ t = 0.5; location_cm = @(0, 0, 0); grip = 0 },
                [ordered]@{ t = 0.6; location_cm = @(0, 0, 0); grip = 1 },
                [ordered]@{ t = 1.8; location_cm = @(0, 0, 0); grip = 1 },
                [ordered]@{ t = 1.9; location_cm = @(0, 0, 0); grip = 0 },
                [ordered]@{ t = 2.4; location_cm = @(0, 0, 80); grip = 0 }) }) | Out-Null
            $s.expect.effector_grabs = 1
            $s.expect.grab_events = 1
            $s.expect.min_grab_frames = 90
        }
        if ($kind -ne 'hold') { $s.expect.measures['control.behaviour.fear_hits'] = [ordered]@{ min = 2 } }
        $s.expect.end_state = 'standing'
        $s.expect.min_get_ups = 1
        # (No limit on the energy: the protective pose draws the knees up and the get-up lifts the body; protect_near_lying gains 51.7 J
        # as the cover comes in, the muscles' work on the legs.)
        $s.expect.measures['control.behaviour.begun_near_afraid'] = [ordered]@{ max = 0 }
        $s.expect.measures['control.pose.target_step_max_deg'] = [ordered]@{ max = 110 }
        if ($p.phase -eq 'cover') { $s.expect.measures['control.behaviour.protect_s'] = [ordered]@{ min = 5.0 } }
        else { $s.expect.measures['control.behaviour.lowerings.0.decided_s'] = [ordered]@{ min = 4.5; max = 4.7 } }
        Save $s
    }
}
