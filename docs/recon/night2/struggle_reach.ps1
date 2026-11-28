# The struggle's flips (160 degrees as an arm drawn in to 25 cm is let go into the protective pose) against how far it draws the limbs in:
# the suite's scenarios that the struggle moves, and the drags on twenty seeds, each with the struggle on (28 November).
#   powershell -ExecutionPolicy Bypass -File docs\recon\night2\struggle_reach.ps1
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$log = Join-Path $repo 'Saved\Recon\night2'
$sets = 'behaviour.struggle=1|behaviour.struggle=1,behaviour.struggle_pull_in_cm=45|behaviour.struggle=1,behaviour.struggle_pull_in_cm=45,behaviour.struggle_leg_in_cm=60|behaviour.struggle=1,behaviour.struggle_pull_in_cm=45,behaviour.struggle_leg_in_cm=70'
$metrics = 'control.pose.target_step_max_deg,control.pose.target_step_joint,control.pose.target_step_t,control.pose.command_jump_max_nm,effectors.list.hand_left.grab_slips,effectors.list.hand_right.grab_slips'
$names = 'command_refused_held,drag_lying_wrists_struggles,drag_lying_ankles_struggles,struggle_phase_hit,struggle_phase_hold,struggle_phase_push,getup_drag_wrists_crouch,getup_drag_wrists_rise,getup_drag_wrists_sit,getup_drag_ankles_sit,getup_drag_ankles_crouch,getup_drag_ankles_rise,grab_drag_fallen_two,grab_overload_tears,drag_lying_ankles_still,drag_lying_wrists_still'
& powershell -ExecutionPolicy Bypass -File Tools\sweep.ps1 -Name n2_reach_suite -Scenarios $names -Sets $sets -Metrics $metrics *> (Join-Path $log 'reach_suite.txt')
Write-Output ((Get-Content (Join-Path $log 'reach_suite.txt') | Select-Object -First 1) -join ' ')
$sets20 = 'behaviour.struggle_chance=0.5|behaviour.struggle_chance=0.5,behaviour.struggle_pull_in_cm=45|behaviour.struggle_chance=0.5,behaviour.struggle_pull_in_cm=45,behaviour.struggle_leg_in_cm=60|behaviour.struggle_chance=0.5,behaviour.struggle_pull_in_cm=45,behaviour.struggle_leg_in_cm=70'
& powershell -ExecutionPolicy Bypass -File Tools\sweep.ps1 -Name n2_reach_seeds -Files (Join-Path $log 'struggle_seeds20') -Sets $sets20 -Metrics $metrics *> (Join-Path $log 'reach_seeds.txt')
Write-Output ((Get-Content (Join-Path $log 'reach_seeds.txt') | Select-Object -First 1) -join ' ')
'REACH DONE'
