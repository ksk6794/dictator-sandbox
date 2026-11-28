# Pitfalls: what has already bitten

Grouped by area, and within an area in the order found. The entries that spoil a result in any part without a sign are in
[`CLAUDE.md`, "Things that have already bitten"](../CLAUDE.md#things-that-have-already-bitten), and only there; read those first.
Before a part's plan, read the areas below that its work touches; the plan names them. An entry marked "(historical: ...)" is about
code that has gone; its lesson still holds. A new pitfall goes into its area here; one that can spoil any part without a sign goes
into `CLAUDE.md` instead.

| Area | Read it before |
| --- | --- |
| [Solver, Chaos and Physics Control](#solver-chaos-and-physics-control) | A change to joints, drives, contacts, constraints or solver settings, or a new force on a body |
| [Measuring, runs and tests](#measuring-runs-and-tests) | A new metric, check, limit or scenario, or a figure that surprises |
| [Muscles, poses and the standing body](#muscles-poses-and-the-standing-body) | The muscles, the poses, the idle, balance and steps, the push matrix |
| [Falls and the get-up](#falls-and-the-get-up) | The fall, lying, the get-up, the authority's springs, the stand of falls |
| [Hands, the grab and blows](#hands-the-grab-and-blows) | The effectors, the grab, strikes and the kick, props, scripted hands |
| [The session, recordings and the headset](#the-session-recordings-and-the-headset) | A session, a recording or its replay, anything in the headset |
| [PowerShell, scripts and building](#powershell-scripts-and-building) | A new or changed script, a C++ build that fails |
| [Assets, the MetaHuman, maps and lighting](#assets-the-metahuman-maps-and-lighting) | The physics asset and its spec, the generator, the MetaHuman, maps and lighting |

## Solver, Chaos and Physics Control

- **Do not make one ragdoll body kinematic to hold it in place.** Kinematic bones follow the component, and
  the component follows the simulated root, so the body is dragged along. Constrain it to the world instead
  (see the `pin` event in `UScenarioRunnerSubsystem`).
- **Physics Control takes its muscle targets from the mesh's pose after the mesh has ticked.** With no
  animation that is the reference pose, constant, as wanted. World-space controls must not use that pose
  (`bUseSkeletalAnimation` off): it is relative to the component, and the component follows the simulated
  pelvis. `USandboxBodyController` ticks the Physics Control component by hand, after the mesh.
- **Leave Physics Control's acceleration mode off for muscles.** It is the plugin's default and scales each
  drive by the inertia of the two jointed bodies only: an ankle got about 18 N*m/rad and the pose crept
  under the body's weight. The controller uses force mode with stiffness in N*m/rad. Chaos multiplies
  angular drive stiffness and damping by 1.5; the controller divides that out.
- **Eight solver position iterations are not enough for a standing body.** A heavy body on light feet
  through stiff joints does not converge: the feet rock at 0.5 rad/s and walk across the floor. The
  controller asks for 64 on the NPC's own bodies (`solver.position_iterations`); it costs 0.4 ms per frame
  instead of 0.26. Shock-propagation and mass-ratio console variables made no difference.
  Measured again in M3 on a body that does not sleep, the gap is smaller: feet at 0.23 rad/s with 8, 0.12
  with 16, 0.15 with 64, pelvis drift 1.5, 1.2 and 1.0 cm. Part of the M2 figure was the sleep below.
- **Chaos's inertia conditioning is global and slows a fallen body's rest.** Raising
  `p.Chaos.Solver.InertiaConditioning.Distance` (default 20) calmed the wrist and the standing feet, but a
  lying body then took 3.3 to 6.7 s to come to rest instead of 2.6. The project scales one body's inertia in
  its spec instead (`inertia_scale`, the hands).
- **A passive body runs on the engine's 8 solver iterations; the NPC's 64 come from its controller.** A
  figure taken from a passive scenario is not the NPC's: hanging from one hand, the wrist stretches by
  2.9 cm at 8 iterations and 0.5 cm at 64 (`-CVars "p.Chaos.Solver.Iterations.Position=64"` to compare).
- **`FConstraintInstance::GetCurrentSwing1`, `GetCurrentSwing2` and `GetCurrentTwist` are not the angles the
  solver limits.** They are the twist of the whole turn about each axis. Compared with the limits they read a
  wrist that swings and twists at once as 40 degrees outside a range it was inside of, and that made hard
  limits look as soft as soft ones. `FSandboxPoseRig::LimitExcessDeg` measures as the solver does; a run's
  `metrics.worst_limit_angles_deg` and `worst_limit_ranges_deg` show the joint that was furthest out. When a
  figure does not move with the setting that should move it, suspect the figure.
- **Continuous collision detection does not keep a heavy body on light feet out of the floor.** A passive
  body dropped from 1.5 m sinks 8.3 cm in and is thrown back out with 149 J, with the feet swept or not; with
  64 solver iterations instead of the engine's 8 it sinks 1.5 cm. It is the solver not converging, not a body
  passing between two steps. A body under control has the 64.
- **A simulated body at rest falls asleep, and a new target for a joint drive does not wake it.** A hand
  that had rested for 2 s stayed where it was while its spring asked for 1900 N. `FSandboxWorldSpring` wakes
  the body it pulls on every update.
- **The engine caps a joint drive's force axis by axis, not by size.** Along a diagonal a drive with one
  cap for all three axes is up to 1.7 times stronger than that cap. `FSandboxWorldSpring::AxisCapsN` sets
  the three caps every frame in proportion to the direction of the force asked for.
- **A drive's target is measured in the frame of the joint's second body.** With `InitConstraint(Body,
  nullptr)` that is the world, so with an identity `Frame2` the targets are world poses. (Chaos solves with
  the second body first: its index 0 is the engine's `Body2`.)
- **A contact's peak force is not limited by a spring's force cap.** A moving hand that is stopped gives up
  its momentum too: 14 N*s in one frame reads as 1270 N. Check a cap with a hand held against something
  (`final_contact_force_n`), as `effector_force_cap` does.
- **Contact events arrive once per pair of bodies and substep, and the impulse vector's sign follows the
  solver's order of the pair.** Use `Hit.ImpactNormal` times the impulse's size to add them up. The floor
  under the feet reports the character's weight in every frame (8.7 N*s at 90 Hz, above the flinch
  threshold), which is why the character counts only contacts with other simulated bodies.
- **A skeletal mesh's bodies ask for contact events only when the component's own flag is set too.**
  `USkeletalMeshComponent::SetNotifyRigidBodyCollision` sets the flag of every body and not the component's
  `BodyInstance.bNotifyRigidBodyCollision`, and `FBodyInstance::BuildBodyFilterData` wants both. With the
  bodies' flags alone the character heard of a contact only when the other body asked for events itself, as
  the hands do: every hand scenario passed while the floor, and any prop to come, went unreported. Found
  in step 5 because the overlay had no foot contacts to draw. `USandboxBodyController::Start` sets both.
- **A body spawned in the middle of a frame is ticked in that frame, before its mesh has a pose.** The
  session's reset does that: the new character's controller ran with no bone transforms to take the muscles'
  targets from, and Physics Control logged 30 warnings ("Failed to find bone data", two per muscle) that no
  script looked for. The controller now waits for the mesh's first pose. A body spawned before the world's
  first tick never had this, so no scenario changed.
- **A component's physics state is destroyed before its `OnUnregister` runs.** A constraint on the
  component's own body is released in `OnDestroyPhysicsState` (`USandboxEffectorComponent` does so for its
  spring), not in `OnUnregister`.
- **Chaos sweeps a body with CCD only when it moves more than 0.4 of its smallest extent in one substep**
  (`p.Chaos.CCD.EnableThresholdBoundsScale`). The 9 cm hand at 6 m/s stays under that: the run is identical
  with CCD switched off. From 6.5 m/s it is swept. With CCD nothing tunnels up to 28 m/s; without it a
  hand at 25 m/s ends 6.5 cm inside the chest (`-CVars "p.Chaos.Solver.UseCCD=0"` to compare). Far above
  strike speeds CCD has a price: from 22 m/s some hits deliver about half the impulse they do without it.
- **Physics Control damps a muscle against the target pose's own turning speed** (`AngularTargetVelocityMultiplier`,
  1 by default). A target that jumps in one frame is then a blow: a leg's target that moved 20 cm at lift-off put
  600 J into the body and snapped the calf at 84 rad/s. The muscles' `pose_velocity_share` is 0; only the swinging
  leg, whose target is a smooth path, follows its target's speed (`balance.swing_pose_velocity_share`).
- **A contact's friction is not the body's.** The engine combines the two surfaces of a contact, by the average
  unless one of them asks for a mode with a higher number (the smaller, the product, the larger), and the floor
  and the hands have the engine's 0.7 and 0.3: `surface.friction` 0.3 is 0.5 on the floor, and under the average
  no value of the body gives less than 0.35. A material's "override combine mode" flags are read by nothing. What
  the floor gives is measured (`Tools\surface-friction.ps1`), and a new value of a material reaches the solver
  only through `FChaosEngineInterface::UpdateMaterial` (`USandboxBodyController::ApplySurface`).
- **A passive body keeps the engine's default surface.** The surface values are the controller's; `drop_*` and
  `impulse_passive` slide and bounce on 0.7 and 0.3.
- **A skeletal mesh answers an overlap query as one thing.** `FOverlapResult::ItemIndex`, by the engine's own
  comment the body's index, came back -1 from `OverlapMultiByObjectType`. To learn which body of a character is
  near, ask each (`FBodyInstance::OverlapTest`), as `USandboxEffectorComponent::FindGrabbable` and the runner's
  hand probe do.
- **A joint's "force" from the engine is its impulse over the last substep of the frame** (`FConstraintInstance::
  GetConstraintForce`): divide by the substep. Calibrated on a hand that drags at a steady 231 N of its spring
  (124 kg*cm/s at 180 Hz). A comment in the solver speaks of force times the step squared; the number does not. And a
  largest force read once a frame misses a jolt in the first of two substeps.
- **A contact event has one point of the contact, the nearest, with the impulse of all of them.** A foot's centre of
  pressure made from the events' points lay 8.6 cm behind the body's centre of mass. Where a force goes through is in
  the solver's manifold points with their own impulses, after each substep (`FNaturalMeasures::AddPressure`); the last
  substep alone will not do either.
- **The solver moves the bodies after its drives.** Joint projection and unfinished iterations turn a joint by 0.2 to 0.35
  degrees after the drive has acted, so a torque worked out from the spring law is off by about 0.005 rad times the
  stiffness: 4 N*m at 800 to 1200 N*m/rad. With 200 position iterations and no projection the law reads the known
  answer to 0.5%. Projection conserves no momentum: it is why the floor's push on a standing body adds up to 0.995 of
  its weight.
- **A torque put on a body is not a muscle's torque.** Chaos solves its joints with an inertia conditioned for stability
  (`p.Chaos.Solver.InertiaConditioning.*`: a rod jointed at its end gets 1.5 times its inertia), and integrates an outside torque
  with the body's own: the arm's weight given as `AddTorqueInRadians` on the arm and the opposite on the shoulder held it 1.6 to
  1.7 times over (with the conditioning switched off, as worked out). `FBodyInstance::SetInertiaConditioningEnabled` on the
  character's bodies while a run goes changed nothing: the solver does not take it. Asked of the muscle's own drive instead (a turn
  of its target by torque / stiffness) the torque arrives whole: the hand of an arm held out ends where an arm of 1000 N*m/rad holds it
  (128.8 cm against 128.7). The spring's figure (`effort.joints.<joint>.torque_final_nm`) read 3.2 N*m left over all the same, at every
  stiffness from 10 to 80 and every angle from 24 to 70 degrees, and was taken for a delivery of 65 to 75% for a night. A figure that moves
  with neither the stiffness nor the angle is a figure to check against where the body ends: a stiff arm's place is the known answer.
- **In Chaos 5.8 a body's `FMaskFilter` filters scene queries only, not the simulation.** The patch of the player's capsule
  (`docs/recon/m4_part5_body.patch`) gave the hands and the capsule masks with a shared bit so that they would not collide;
  `FQueryFilterData::NarrowFilter` reads the mask, the pairs of the simulation (`FShapeUnionFilterData::BroadFilter`) read the
  channels and the block masks alone. Bodies that must pass through each other and block everything else need object types that
  ignore each other: the capsule is `PlayerBody` (`ECC_GameTraceChannel1`, `Config/DefaultEngine.ini`), which the hands and feet
  ignore. Checked by a scenario in which a hand and a foot go through it (`body_hands_pass`), not by reading.
- **A collision response set in a component's constructor drops the profile named in it.** `SetCollisionProfileName` in a
  constructor only marks the profile as pending, to be applied in `PostInitProperties`; `SetCollisionResponseToChannel` after it
  clears the pending mark (`FBodyInstance::SetResponseToChannel`), and the component kept the engine's default responses and object
  type. Every hand became a body of no profile; two scenarios with two holding hands changed their hashes (one hand's impulse 114
  to 724 N*s) and every other kept them. Set a response after `Super::PostInitProperties()`, before any physics body exists
  (`USandboxEffectorComponent::PostInitProperties`).

## Measuring, runs and tests

- **A strike scenario can pass because it lands on a lucky spot.** `strike_punch_forearm` passed M2 at
  10 rad/s; 2 cm higher the same punch snapped the wrist at 37 rad/s, and adding the collarbones moved the
  arm enough to land in that band. Sweeping the point of impact (and the stiffness: 11, 49, 41, 35, 30,
  38 rad/s for 100 to 300 N*m/rad) showed it. A figure that jumps back and forth as a setting changes
  smoothly is a scenario on an edge; strike more than one point (`strike_punch_forearm_heights`). `strike_punch_shoulder`
  was another: at its point the collarbone turned at 8.2 rad/s, 2 cm above or below it at 26 to 31, and the head as a body
  of its own moved the contact by a frame and showed it (30.7; the same 26 to 31 off the point on both bodies).
- **A pose of zeros must leave a run bit for bit as it was.** `FSandboxPoseRig::Build` keeps the reference
  rotation itself for a joint at 0, and the animation instance writes only bones some pose has turned:
  `Ref * Frame * Identity * Frame^-1` need not round to `Ref` exactly (not measured; the code does not
  rely on it), and a hash moves with the last bit.
- **Do not judge "at rest" by the fastest body.** A hand trembling against the floor (0.5 kg) kept a lying
  character "falling" for good. The controller uses the mass-weighted root mean square over all bodies.
- **Scenario time and controller time differ by one frame:** the controller's first tick comes one frame
  after the body is spawned. Result files use the run's own frame count; do not mix the two. A threat's time in `control.perception`
  is the body's clock; held against a hand's first contact (the run's clock) it was first read 0.011 s off (0.068 for 0.079).
- **The JSON reader takes a number for a boolean and a boolean for a number.** `TryGetBool` succeeds on `1`.
  Where the kind matters (scenario expectations, tuning values), check `FJsonValue::Type`.
- **Automation tests fail on any logged error.** Code that tests call must hand its errors back
  (`FString& OutError`) and leave logging to the caller, as `FSandboxParamStore` does.
- **What a strike does is over long before a run's last frame.** `-Shot` alone shows a standing character.
  Use `-ShotAt` with the times of the hit, and `-Overlay all` to see the contact and the flinch.
- **Overlay text reaches screenshots through the HUD's actor overlays.** With `AHUD::bShowOverlays` set and
  the actor added by `AddPostRenderedActor`, `DrawHUD` calls the actor's `PostRenderFor` with the scene
  canvas, whatever class the HUD is (the engine's plain `AHUD` in a scenario run, `ASandboxHUD` in a
  session). `AHUD::OnHUDPostRender` hands out the debug canvas instead. The lines are a `ULineBatchComponent`
  the overlay owns, emptied and filled again every frame, in the foreground depth group so that they show
  through the character.
- **`Sandbox.Tune` takes all of its values or none, and with one out of range the run goes on unchanged**; it says so only
  in the log. A scenario's console event did that (`step_reach_cm=500`, range 300), and a check scenario whose one value was
  out of range ran the untuned body and measured that. A scenario that tunes expects `tuning_changes`.
- **A motion of a few centimetres does not show on a sheet of pictures.** The idle moves the hands by 4 to 7 cm
  and the head by 4; the film's tiles of a standing body with and without it look the same. Such a motion is
  measured (`body.bodies[].settled_travel_cm`, `metrics.settled_head_travel_cm`) and held by a check
  (`min_settled_head_travel_cm`); the look of it is for the owner in a session.
- **A body that moves by design needs limits that say so.** With the idle, `stand_npc` broke its limit on the
  fastest body after settling (0.05 m/s, written for a statue; the hands go at 0.08) and `pose_lean` its
  `min_pose_offset_deg` of 29.9 for an elbow asked for 30 (the idle turns it by up to 3). Jitter is still
  watched by the angular speed and the joint speed.
- **`body.bodies[].final_location_cm` is a body's centre of mass from where the character was spawned, not a place
  in the world.** Read as world coordinates in a room, it put eleven bodies "standing inside the table". World
  positions over a run are in `trace.json` (bone origins, every 0.5 s).
- **"In motion" by the mass-weighted speed lasts long after a body has stopped:** limbs go on settling above the
  threshold. Friction worked out of the time a dragged body was in motion read 0.55 where the acceleration gave
  0.69. Check a new measure on a case whose answer is known.
- **A run that only asks whether the body ends standing need not run to its end.** Most bodies of the stand are
  on their feet 8 s after the blow and were simulated for 40 s. `end_when_standing_s` ends a run once every event
  is over and the body has been Standing for that long without a break; checked against the full-length runs
  trial by trial before it became the default.
- **A flat face turned from the stage's one light is black on black.** The open hand's plate did not show on a
  scenario's picture at all; the overlay's hands layer draws it as its edges (`-Overlay hands`). Before
  concluding that a thing is not there, draw it.
- **A limit that a missing thing meets is no check.** `max_prop_travel_cm` passed for a prop that was never
  made (it had travelled 0 cm). A run with props checks that they are there (`props_simulated`).
- **A measure can be met by what it was not meant for.** "A hand touches before the trunk" read 100% of forward falls: the
  hands land beside the hips and the head strikes the floor; asked for a hand ahead of the chest, it still read 86%. Look at
  the motion (`film.ps1 -Around`) before a criterion that is met is believed, and run a sharper rule before saying what it
  will read.
- **A check that a thing is its own inverse does not say it is right.** A pose database mirrored without changing sides
  passed "mirrored twice it is itself". Break a measure on purpose and see its check fail before relying on either.
- **The largest change of a command in a frame is not its largest jump.** A swinging leg's is 65 N*m, and 4 of them are a
  jump: read `command_jump_max_nm`. And a frame can have several causes: of 365 N*m 333, 333 and 140 were left with each of
  three put right alone, 26 with all.
- **A tuning file edited while runs go is read by them.** Every scenario run looks at `Config/Tuning` every 30 frames and reloads it
  (hot reload). On 27 November 2026 a value added to `body.json` for code not yet built went in while three stands ran: the running
  processes refused the changed file and kept their values (an error in each run's log, the physics as before), but every process that
  started after it could not read the tuning at all, and one stand of 150 ended with no result. A build that does not know a name
  refuses the file ("is not a tuning value"): add a name and its value in the same build, change the tuning files only while no
  engine process runs (or put the new value in after the build that knows it, before the runs), and run a stand that spans an edit again.
- **Eight falls are not a measure of a fall.** R3's catch and head kept the head off the floor in 8 of the 8 knockdowns by blows (5 of 8
  struck today); on 41 blows of 160 and 240 N*s from 24 directions it struck in 27% (29% today). The knockdowns and the scorecard are a
  few directions at one impulse; a change to how the body falls is judged on many (`knockdowns.ps1 -Directions 24 -Impulses "160,240"`,
  `-ByHand -Directions 24 -ShoveMps 3.5` and `4.5`, run through one sweep), and the few are reported beside them.
- **Check a limit against today's body before it judges a change.** "No joint faster than 30 rad/s in the knockdowns" was written
  for N6 and today's neck reaches 36 after the blow at 90 degrees; the plan said "the arms, when they meet the floor". Read today's
  figure of a new limit before the runs it is to judge.
- **`film.ps1 -Around` is where a film begins (0.1 s before it), not its middle.** On 28 November two films of the commands were asked
  for "around" the moment to be seen with half the span on each side; they began there and missed the lowering and the first motion
  they were made for. Give `-Around` the first moment to be seen, and read the sheet's first time before the film is looked at.

## Muscles, poses and the standing body

- **A muscle target that jumps puts energy into the body.** The flinch's pose offset first ended with the
  Flinch state; a hit that knocked the body over ended it in one step, the muscles snapped the falling body
  back, and the run gained 13.8 J (`max_energy_gain_j` caught it). Anything added to the target pose fades by
  its own clock, not with a state.
  The same holds for a foot placement switched on at full weight (the calf snapped at 84 rad/s): weights
  ease in over `poses.blend_s`.
- **A point fixed in the world is a moving target for a body that turns.** A leg placed at a world point
  on a body pinned by a ball joint tipped the body, the point moved against it, the leg chased it, and the
  body fell. Test a placement on a welded pelvis (`"lock_rotation": true` on a `pin` event).
- **A sustained push by a hand made the character flinch again every time a flinch ended** (0.35 s), until
  it fell: each contact frame added to the felt impulse. Since M3 part 3 only the impulse beyond the
  smoothed pressure is a hit (`reactions.pressure_s`): one flinch as the hand lands (`effector_hold_chest`).
- **A capture point compared with the support "now" lets the assist hold anything.** It runs away by
  e^(omega t) while a step is under way; compared now with a 30 cm reach, a 200 N*s push was held by the assist
  with 110% of body weight. Compare where it will be when a step lands (`balance.step_time_s`).
- **A decision that waits lets the assist take back the blow.** The capture point is the whole body's momentum
  and a blow gives it in one step; held for 0.05 s, a 200 N*s blow was undone by the assist (three times the
  body's weight) in three frames. Losing balance is decided at once; only "a step is needed" waits.
- **A foot below the standing threshold is still support.** After a sideways blow the far foot carried 14 to
  30 N, under the 20 N of a standing foot; the support polygon shrank to one foot and a step "was needed".
  Decisions take every foot the floor touches (force above 0).
- **Leg turns must not depend on each leg's load.** Weighting the strategies' turns by load gave a feedback
  loop: the lighter leg turned less, the body rolled onto the other, and under a held push it rocked from foot
  to foot with the assist at 0.43 of body weight.
- **Sideways, turning the ankles' targets after a blow lifted the unloaded foot, whichever way it turned**
  (`balance.ankle_side_gain` was 0, taken out on 28 November 2026). A parallelogram lean (ankles and hips together) did the same.
- **Leaning into a held push needs the ankle targets to tilt.** Moving only the strategies' reference and the
  assist's anchors left the ankle muscles holding the upright pose: the assist carried the whole 40 N push.
- **A filter that lets a blow count as pressure counts its momentum twice.** A blow's momentum is in the
  capture point already. The lean and the decision use the force felt in every frame for
  `reactions.held_push_s` (0.3 s); a shove of 0.1 s is not a held push.
- **A hand held at a point pushes harder the more the body leans into it.** Moving the target 4 cm out lowered
  the force only from 138 to 104 N. Test a lean with a scripted force (`impulse` with `duration_s`).
- **A falling body kept reaching for a placed foot's point** and gained 1.7 J; the controller now lets placed
  feet go (eased) when it falls.
- **A leg's target must never jump, whatever changes under it.** Every change of a foot's point or of the frame
  its leg is solved in glides from what the leg was solved for last (`FFootPlacement::GlidedPoint`,
  `FSwing::StartOffset`), not from where the foot happens to be.
- **A foot that has just landed stands on an edge.** The decision took the edge for the whole support and ordered
  a second step after a good one; while the foot settles (`balance.step_settle_s`) its whole sole counts.
- **Braking a stagger through the planted legs lifts the near foot** when it is strong (`balance.stagger_damping_s`
  0.2 against 0.15): the body rebounded off the new support and stepped again.
- **A body that decides it cannot be saved goes over like a plank.** Until the audit of November 2026 a capture
  point beyond one step's reach meant Falling at once: the muscles let go and the body tipped over its heels
  without having moved a foot, also after an 80 N*s push backward that two steps catch. It now always steps, and
  falls when the pelvis tilts or sinks (`thresholds.fall_*`).
- **The collarbones are trunk, not arm.** In the arms' group, with arms soft enough to give way to a hit
  (40 N*m/rad), each collarbone sagged 10 degrees under its arm and laid the arm against the body.
  `FSandboxBodyTuning::GroupOfBone` puts them with the spine.
- **A step that brings the feet back together after a lunge is not an asked step from a stance**
  (`UpdateRegroup`). Four things kept it from working, each found by reading what else acts on the body, none by
  tuning. The ankle strategy's reference was the middle of the feet: it brought the body back while the weight
  shift took it forward (5 cm of 19), so a shift is now part of that reference. The foot left behind held the
  pelvis 15 cm down for a reach it no longer needed: the body sat on the bent stance leg with its centre of mass
  over the heel; a leg lowers the pelvis only by the share of the weight it carries. Waiting for the body to
  stand over the stance foot took a goal far beyond it (the body went on past the foot): the foot lifts when the
  step will hold, the capture point at landing inside the support to come. And once the foot has landed the
  anchors glide from where the body is, so what was left of the shift counted twice and threw the body off the
  far side.
- **"Not upright" is not "falling to the floor".** A body hung up by one hand took the pose of a fall backward
  and held it for good (119 J of muscle work, a pendulum with its knees at its chest). A body that touches
  nothing of the world and does not come down is held up (`UpdateHeld`, `thresholds.held_*`) and hangs in its
  stance's pose; the state is still called Falling, or Lying once it is still.
- **A body on its toes still has its feet under it.** The support polygon is what touches the floor; with the
  heels 1.5 cm up it is the toes alone, and a capture point under the raised heels counted as outside the
  support: a body hit on the back of the head stepped back twice (the one open cell of the push matrix from part 4
  to the audit). Decisions (`DecisionMarginCm`) take the whole sole of every foot the floor touches.
- **A foot on its toes has a sole polygon of its toes only** (`FSandboxBodyProbe::SupportPolygon` takes what lies
  within `sole_tolerance_cm` of its lowest point). Its middle is 12 cm ahead of the whole sole's: where a foot
  will stand, or the place over a foot, is taken with a tolerance that takes in the whole sole.
- **The idle clip is read from its keys, not from its compressed data** (`FSandboxIdleMotion::Load`): the editor
  builds the compressed data in the background, and a run must not depend on whether that has finished. The
  clip is Epic's content, fetched and not committed, and so is nothing derived from it.
- **"The elbow cannot straighten" was written down and believed for two parts; the joint can.** The stance never
  asked. Before planning a change to the asset for something a joint "cannot" do, ask it for the pose
  (`-Tune "poses.stance.<joint>.swing1_deg=..."`) and look.
- **More grip is not more stability.** With the soles at 0.9 on the floor a blow of 200 N*s on a thigh tripped
  the body where it had stumbled and stood, and after a push sideways it froze on crossed legs, in Stagger, for
  the rest of the run; the push matrix called both "stepped" or "fell" as allowed and passed. And a body with 0.9
  against the floor gets up worse: turning into a clip's first pose and coming onto hands and knees it has to
  slide.
- **A step counted from outside is a foot that left the floor for 0.1 s and stood again 5 cm away, and a
  shuffle is just over that.** As a stepping foot lands, the other one hops 5.0 to 6.1 cm, and the push matrix
  read one recovery step as two: the cells that 32 to 48 solver iterations and most weaker assists "lose" in the
  30 N*s row are that, by the controller's own log (`control.recovery_steps` against `control.steps`). Before a
  failed cell is taken for behaviour, read both. (The push matrix counts a step from 10 cm since the owner's
  decision of 24 November; the run's own record still starts at 5 cm.)
- **Two questions asked of two different supports leave a gap between their answers.** "A step is needed" was
  asked of the whole soles with 4 cm allowed, "the body stands again" of the part of the soles that touches. A
  body on the edges of its feet, 2.4 cm outside the one and 6 cm outside the other, did neither, for good, and
  rested there because the stagger's upright hold on the pelvis (up to 150 N*m: 19 cm of an 80 kg body) carried
  it. An allowance for what a body in motion brings back does not hold for a body at rest; a body at rest with
  its centre of mass off its support is being held by the assist. When a state is never left, write down every
  condition of every way out of it and look for the values that satisfy none (`series.csv` has the three
  margins: `com_margin_cm`, `capture_margin_cm`, `decision_margin_cm`).
- **A run that ends in Stagger is not always stuck:** a step that brings the feet back together is made in
  Stagger, and one that lands a moment before the run's end leaves that state in the result.
- **A push matrix of 160 of 160 can stand on a threshold.** The blow of 30 N*s on the head from behind and to a
  side leaves the capture point just under the 4 cm that call for a step; any small change to the body puts it
  at 4.4 cm, the body steps, and the cell is judged by how its feet shuffle.
- **A glide that is cut short has to be gone on from where it was cut.** `PlaceFoot` without `bFromLast` starts a change of
  frame from the frame the leg was going to, not from the one it was last solved in: 21 cm, 27 degrees of the thigh in one
  frame after a lift. Repaired by `balance.lift_smooth` (on since 26 November 2026; `docs/natural-body.md`, decision 13).
- **What held the head back from turning was the world.** "A slap does not turn the head because the neck is stiff and the head
  round" was written on 25 November; over necks of 40 down to 1 N*m/rad the turn went from 6 to 15 degrees only. The assist's hold
  on the head (10 N*m/rad, 0.2 s: a damper of 2 N*m*s/rad on 0.02 kg*m2) took most of it. When a figure hardly moves with the
  setting that should move it, list every force on the body, the world's among them.
- **A leg's two-bone placement does not place an arm.** Taken over as it was, the pole put the stance's elbow into its own plane
  in the target's first frame (the arm's target turned 21 degrees in one step), and the hand kept the orientation it hung with, so
  an arm held out asked its wrist for 90 degrees; the wrist pushed on its limit and the hand ended 30 cm from its point with arms
  at 3000 N*m/rad. An arm's bend goes over from the pose's plane with the weight, its hand turns with the forearm, and its shoulder
  and elbow are held to their ranges (`FSandboxLimbTarget::bArm`). Test a placement with stiff muscles first: that is the geometry;
  what the arm's own strength leaves of it is its weight.
- **A hand placed near its own shoulder turns the elbow over.** R2's held arm, pulled in, puts its hand `getup.arm_pull_in_cm` from
  the shoulder toward the holding point (27 November, step 12). Worked out anew every tick, the way turned round as the holding point
  passed the shoulder and the arm's target jumped 160 degrees in a frame; held in the chest's space, it still jumped 148 degrees at 25
  cm once the holding hand followed the arm: the placed hand came near enough to its shoulder for the two-bone placement to turn the
  elbow over. At 35 cm the largest jump is 29 degrees, the clip's own. The suite's `control.pose.target_jump_max_deg` told both; read it
  for every new target that a placement makes, and keep a placed hand well away from its shoulder.
- **A placed arm blended by where its hand is passes its shoulder on the way.** R3's catch (28 November) put each hand at 0.9 of the
  arm's length toward the floor; coming in and going out the placement blends the hand's point between the pose's hand and the target,
  that point passed near the shoulder, and the elbow's target turned over by 170 to 178 degrees in a frame in 3 to 9 of the suite's
  scenarios, the target itself never near the shoulder. Blended by the joints' turns (`FSandboxLimbTarget::bJointBlend`, the guard's) it
  does not, and the arm comes in later on that path. `control.pose.target_step_max_deg` over the whole suite with the switch on tells it;
  the scenarios' own limits did not.
- **An elbow's pole that lies along the arm leaves the bend undefined.** R3's catch sent the elbow "back and out", as a reach's; for a
  hand reaching back and out that is nearly the arm's own line, and the upper arm's target turned over by up to 172 degrees in a frame
  in 22 of 96 falls, none in the suite. The pole is chosen across the line from the shoulder to the target. A placement is measured on
  many directions of the motion it serves (`control.pose.target_step_max_deg` over every run), not on the suite alone.
- **A hinge signed by a carried fallback flips when the carrying rotation is near half a turn.** The two-bone placement signs the
  elbow's (or knee's) hinge against the pose's hinge carried by the least rotation from the pose's upper bone to the solution's; when the
  two point opposite ways that rotation, and the sign, are undefined, and the arm's target turned over by up to 173 degrees (R3's catch
  over the fall's reaching poses). An elbow bends about one axis of its upper arm: for the catch's arm the hinge is that anatomical axis
  (the reference pose's, carried by the upper arm, before; the one across the pole's plane after; `FSandboxLimbTarget::bAnatomicHinge`).
  The first repair, the bend's own axis above a threshold of the bend and the old sign below it, kept a turn-over at the threshold, and it
  reached the guard's arm, which nothing in the suite switches on: a rule for one caller is given to that caller alone.
- **A limb's settings outlive the user that set them.** The animation instance keeps a limb end's pole, ease, hinge and ranges
  (`SetLimbBend`, `SetLimbRanges`) until they are set again; R10's struggle drew a leg up with a pole of its own, and the steps' feet,
  which set none, solved with it after: the body got up and staggered, once fell (28 November). A user that sets a limb's settings for a
  limb whose other users set none forgets them as it lets go (`USandboxAnimInstance::ClearLimbSettings`).
- **A limb's solve blended into a pose by the joints' turns flips when the two are nearly half a turn apart.** R10's struggle drew a hand
  in to 25 cm from its shoulder and a foot to 45 cm from its hip; let go into the protective pose (a forearm over the face), the upper arm's
  target turned by 160 degrees in a frame, a calf's by 152 (28 November, with the struggle on). A pole carried with the trunk did not help;
  drawing in to 45 and 70 cm did. A placement far from the pose it is let go into is measured on its release, not only while it is held.
- **A measure that counts one kind of motion misses another that comes first.** C39 asks for the first change of a target after a command;
  the first motion was recorded at the get-up's start, while the cover had already been let go, in the command's own tick and with no
  reaction at all (28 November). Read a criterion's words against what the code records before tuning toward it.
- **A target let go at a point of the world, or moved to where the hand is, jumps.** A limb target's weight fading over 0.15 s at its
  last point of the world, as the body lay and rolled, turned the arm's target over in the first tick of the fade. Three
  more users of an arm did it in one window (28 November): the offered hand let go from where its hand was (131 degrees), R2's held arm
  let go at a point of the world as a body given up for a drag fell (150), the struggle's leg the same; and a pole worked out again at
  the release (129). Let go from the last target, with the pole kept, carried with the chest until the weight has gone (R3's catch's
  way: `PlaceHeldArm`, `ReleaseOfferArm`).
- **A thing worked out at the first event it serves does nothing before it.** R3's palm turn read the palm's axes at the hand's first
  touch of the floor, so the turn was never laid on before the touch, and the palm's angle at the touch was 81 degrees with every share
  of the turn. A measure that does not move with the setting that should move it was the sign (as in the solver's entries).

## Falls and the get-up

- **The pose of a fall is for meeting the floor, not for lying on it.** Held until the body was at rest it
  made a stiff shape of the body: knocked over by the head, it stood on its head with its legs in the air for
  a second, and on its back it kept its forearms up. The muscles let go `thresholds.impact_let_go_s` after the
  trunk or the head first meets the floor.
- **A recorded pose for a body in the air is not one for a body that lies.** R3's blend of Epic's reaching poses over the
  whole body, kept on as the body lay, held a leg up in the air at rest (the sample's legs trail a body thrown through the air);
  over the arms only, and let go as the body lies, it did not (27 November). Use a recorded pose for the part it was recorded
  for, and film the rest after the fall, not only the fall.
- **A falling body needs a pose of its own and legs that are let go in time.** Holding the stance, it fell
  straight; with a fall pose but its placed feet released over `poses.blend_s` (0.4 s), a body that fell after a
  step met the floor with straight legs. Placements are released over `poses.fall_blend_s`.
- **"Lying" must not wait for "motionless".** With the old rest thresholds (0.05 m/s for 0.5 s) the fall's pose
  kept the body curled up on its back, rocking, and it came to rest after 3.5 s; lying as soon as it is down and
  nearly still (0.3 m/s, 1 rad/s, 0.2 s) it rests after 1.7 s.
- **The muscles of a falling body do work** (10 to 30 J to fold the legs and put the arms out), so
  `max_energy_gain_j` of a fall is no longer near 0; the falling scenarios allow 40 J. Upright it is still 1 J.
- **Letting a clip run further ahead of the body leaves less room before the body counts as lost.** With the
  pelvis allowed 16 cm of height and 20 cm of floor behind the clip instead of 12, the stand went from 45 to 48 of
  50, and a 30 N*s push that the get-up had ridden out now put the pelvis 31 cm off, past `getup.lost_cm` at 30:
  the suite's `getup_push_weak` caught it. The two limits move together. (historical: the clip-following get-up and its
  lags, removed 27 November 2026; `getup.lost_cm` stays)
- **A clip must not wait without end for what will not come.** A foot that lay across the other leg was 29 cm
  from where the clip's first key had it and off the floor; the clip waited for it through six attempts. Bound every
  wait. (historical: the clip-following get-up and its `getup.foot_wait_s`, removed 27 November 2026)
- **The same clip from the same place fails the same way.** A body that gave a get-up up tried again by the same
  clip, five and six times. The matcher prices the motion just given up and its mirror image (`getup.match_given_up_cm`).
- **A body knocked into a wall often does not fall.** Three of eight blows toward a wall 45 to 90 cm away ended
  with the body on its feet against it. A stand of falls draws such a trial again and says how many it drew.
- **A body appears in its reference pose, the arms out, and brings them down in half a second.** The fingertips
  are 57 cm to either side of its axis, at the height of a chair's back. Put 45 cm from a thing at its side, a
  finger appeared 0.12 cm inside the chair and 0.79 cm inside a wall, and no check said so. A thing beside a body
  is 65 cm away or more; `Tools\getup-stand.ps1` refuses a place where the arms would touch
  (`penetration.spawn_world_cm` of a run shows it: 0.034 is the soles on the floor).
- **A get-up that is given up moves the body.** Each failed attempt shifts it by half a metre; a body that fell at
  a wall 166 cm from the table stood, at the fourth attempt, 30 cm from the table. A fall "at a wall" stays one
  only by distance: the stand has those falls in the room's corner quarters and reports how near each came to
  the furniture.
- **A clip that stays where it was put makes the assist drag the body over the floor.** A body that turns into a
  clip's first pose, rolls over or comes onto hands and knees moves 20 to 35 cm, and the clip waited there for the
  pelvis to be pulled back: half the assist's force went into that, and with a weaker assist the clip waited for
  good. The assist was believed to be lifting a body whose arms were too weak; the log line of the failed attempts said
  otherwise (pelvis 30 cm off over the floor, 4 cm in height). (historical: the clip-following get-up, its assist and
  `getup.pelvis_follow_s`, removed 27 November 2026)
- **An assist that is capped always pulls at its cap,** so its share of body weight measures the caps and not the
  need. Sweep the caps down against the stand and read what fails, not the share.
- **A scenario that starts with a fall is written for where that fall puts the body, and every change to the
  body moves it.** With the stance's elbows 20 degrees straighter the blow of `getup_*` left the body on its back
  instead of half on its side: a scripted hand pressed the empty floor, a get-up took 7.3 s instead of 3.7 and
  ran out of its scenario's time, a pressed body needed two attempts instead of one. "300 N costs a get-up 0.2 s"
  had been that one fall; on 150 falls a third of the bodies stay down under it. What something does to a get-up
  is asked of the stand (`-PressN`, `-Tune`, three seeds), and a single scenario holds only what every fall
  shows.
- **No strength of the body draws a line between a light press and a heavy one.** Its legs lift 600 N as they
  lift 300 N, or neither, by the phase of the clip the press finds it in; stiffer legs spoil the get-up nobody
  hinders. What a press does is a rule on the force the body feels (`getup.hold_force_n`).
- **A clip steps a foot that the body is standing on.** A recording brings a foot forward while its weight is on
  the hands or the other leg; the simulated body's weight is where its own feet, hands and friction put it. With
  the clip's time waiting only for the pelvis, the body stood up leaning like a plank from a foot 45 cm behind where
  the clip had it; made to wait for the feet too, it waited for good, at 16 cm, for a foot that carried 525 N. A
  foot the body stands on is where it is. (historical: the clip-following get-up and `getup.frame_follow_s`, removed 27
  November 2026; the led get-up does not lay the clip over the feet)
- **The assist of a get-up takes back a blow unless it is capped and the body may be lost.** With 500 N on the
  pelvis and `getup.lost_cm` at 45, a 200 N*s blow moved the body 40 cm and the assist had it back in half a second
  with 1.2 times its weight. Capped at 250 N (feet 50 N) and lost at 30 cm, the same blow fells it. (historical: the
  clip-following get-up's assist, removed 27 November 2026; the carry's answer is decision 20, a hit takes authority by its impulse, and the entry
  "Half of a stiff spring still takes a push back" below)
- **A scenario about a fall must keep its body down.** With the get-up on for every body, the fall scenarios and
  the knockdowns would measure "at rest for good" at the end of a get-up. A scenario's body gets up only when its
  file says `"get_up": true`; the session's does by default.
- **A clip made for another skeleton cannot be copied bone by bone, even with the same bone names.** The sample's
  mannequin and the MetaHuman body differ in their reference poses by 4 to 21 degrees a bone, partly in how the
  axes lie and partly in how the limbs are held (upper arm 11.6, forearm 12.8, thigh 4.0 degrees apart in
  direction). `FSandboxClipPose` carries over what each bone has turned from its own reference pose, in the space
  of the whole body, and aligns the segments of arms and legs; a run's `control.clips` has the figures.
- **A recorded get-up asks joints for more than the body's have.** Over the sample's four get-ups: the wrist 73
  degrees beyond its range, the foot 48, the shoulder's twist 43, the hip 29. Asked as recorded, the muscles
  pushed into the limits and the mean pose error was 7 to 12 degrees; with every target held to its joint's range
  (`FSandboxPoseRig::ClampToRange`) it is 3. The wrist is a defect of the model, not of the clip: the forearm's
  turn is in the wrist's twist, the two swings are limited in the forearm's frame, so a hand turned palm down has
  its bending back limited by the 25 degrees meant for the sideways bend.
- **A body whose pose is held from outside must not judge itself.** Carried through a get-up by its pelvis for a
  film, it took itself for fallen as soon as the pelvis was laid down, went limp in the Lying state's muscles,
  and the film showed a standing body with its head and trunk hanging. Nothing failed: a film drops the
  scenario's expectations. While a clip's pose is asked for (`SetClipPose`) the controller decides nothing about
  balance; look at the state log of a run made for looking.
- **A weld gives way to nothing.** A body carried by its pelvis through a get-up on the floor (`pin` with
  `follow_clip`, `raise_cm` 0) is pressed 10 cm into the floor and its joints stretch by 6 cm: its limbs are in
  the way and the weld does not care. And the first move, from standing to lying within `poses.blend_s`, swings
  the legs through the floor even at 60 cm (joints at 160 rad/s for a moment). The rig is for looking at poses in
  the air.
- **The sample's clips log load errors that are not failures.** Each names anim notifies of the sample that were
  not copied (`BP_AnimNotify_FoleyEvent_*`); the keys load. An automation test must not load one: it fails on any
  logged error.
- **A count that starts again whenever there is progress does not bound a wait.** `getup.stall_s` (3 s) counts
  while the clip's time stands still; a body that hovers at the lag lets the clip move on by a frame now and
  then, and an attempt lasted 15.6 s, 13 of them in one pose. A wait is bounded by a total (`getup.wait_s`). When
  a limit "cannot" have been passed, look at when its counter is set back. (historical: found on the clip-following
  get-up, removed 27 November 2026; `getup.wait_s` stays as the bound)
- **A scripted force is felt as a press; a pin is not.** `impulse` with `duration_s` goes through what the body
  feels (`SustainedN`), so a get-up under it is "pressed" or "held down". To keep a body from rising without
  pressing on it, pin it (`getup_wait_bounded`).
- **Muscles that chase a point of the world while the body turns do work, and a fall shows it as energy.** Braced arms sent to
  the floor behind a body falling backward gained 83 J (today's fall 30) in the 0.2 s before a hand touched; more damping on them
  left 48 J and kept a blow's fall from rest for 4 s. The 40 J of a fall (`max_energy_gain_j`) catches it.
- **A reference made for another skeleton lies lower than the floor lets this body lie.** Pulled toward a clip's bodies by springs
  from the world, the lying and sitting body was pressed down with 700 to 980 N of its 784 N of weight (its trunk 4 to 10 cm above
  the targets), and that was most of the "assist". The world never pushes a body down (`FSandboxCarry`): gravity brings it down
  unless the floor holds it. When a share of force is far larger than the job needs, log it body by body with its direction.
- **Capping what the world may lift did not make the legs lift.** With the led get-up's upward cap at half a weight per body the
  stand went from 139 first attempts to 70: the body falls behind its clip and the attempt is lost. A cap is not a controller.
- **"Never pushes down" made for a spring's place still pushes down through its damping.** The carry gives a body above its reference
  the reference at its own height, so its spring never pulls it down; its damping brakes a body that rises faster than its reference,
  and on the stand of 150 that was 0.12 of the body's weight downward on average, up to 1.27 weights in a frame. Found only when the
  world's force was measured by direction (`control.authority`, R2's step 2). A rule about a force's direction is checked on the force
  measured by direction.
- **Half of a stiff spring still takes a push back.** Decision 20's hit takes the carry's share in proportion to the impulse; a push of
  30 N*s left half of a spring of 400 N/m per kg damped near critically, and the push was taken back within a tenth of a second as
  before (the chest 2.4 cm off against 2.1). What a share does is measured by what the body does, not by the share.
- **A pull from the world that gets the body up gets it up against a hand too.** Led, two pressing hands did not slow a get-up and
  one holding hand was torn off, until a press took the pull away in proportion; one hand still does not hold. Every new source of
  force is run against the hold and the press (`getup-stand.ps1 -HoldHands`, `-PressN`) before it is called interactive.
- **An angle averaged across its seam points the other way.** The support frame was the mean of the two feet's turns since the start,
  each from -180 to 180 degrees; a body that got up facing about half round from where it started had its feet at 175 and -175, a frame
  at 0, its stance 65 cm "off", a regroup step that turned it round and a fall (27 November, `getup_laid_front`). It showed only on a
  body laid down facing away, and in 6 of the stand's 150 trials. Average turns by taking each on the first one's side of the seam.
  The gaze had the same seam (a target straight behind swung the head between its limits); search for every angle from `Atan2` or a
  twist that is averaged, clamped or compared.
- **A body spawned turned takes that turn for standing.** The controller's references are the pose it starts in: spawned lying
  (`body.rotation_deg`), the assist held it lying and called it a stagger, or the get-up's clip was laid 40 cm from it. A body to lie
  is laid after its control starts (`body.laid_deg`).
- **A clip's name is not what it begins with.** R2's plan had the body roll from its side to its back or front "by the sample's
  `M_ragdoll_getup_roll_L` and `_R`"; read by the motion library (27 November, `control.motion_library`), both begin with the pelvis
  38 to 47 cm up, 2 m from where they end, and only a foot (or nothing) on the floor: they get a body up out of a dive's roll, not a
  lying body over onto its back. Read a clip's first frames (where its pelvis is, what touches its floor) before a plan rests on it.
- **C27's 70 cm lies on a step of the side clips' pelvis.** On `stand_L` the pelvis stays at about 70 cm for a quarter second while
  the trunk comes up from lying flat, so "the trunk as the pelvis first passes 70 cm" reads 57 degrees on the clip and 63 or 80 on a
  body that follows it to 1 or 2 cm, by which side of 70.0 its pelvis is on (R2's step 8: the matcher laid the pelvis a few cm higher
  and half the L get-ups read 80). A figure taken at a threshold is as steady as the motion is where it crosses it.
- **A pointer into the controller's map of clips does not live past the next clip read.** `Clips` is a `TMap`; reading another clip
  may move every one in it. The old choice of a get-up kept the pointer of its best clip while it read the others (it never showed:
  every hash is the same with it found again by its path, R2's step 8). Keep a clip's key, and find it again after any read.
- **A body laid on its side tips onto its back by itself.** R2's roll (27 November, step 9) was first written by intuition, both arms
  forward and the upper knee up, to roll the body onto its front; it rolled it onto its back. Every one of eight changes made one at a
  time from a neutral pose, the neutral pose too, did the same in 0.57 to 0.94 s: this body's round back rocks it backward, and an arm
  thrown forward pushes its shoulder back. A pose to the front had to stop that first (the upper arm across toward the floor in front,
  the upper thigh drawn across), and a pose that came in over 0.9 s instead of 0.4 s was too late for both front rolls. Measure what a
  pose does to a body as it lies, one change at a time (`sweep.ps1 -OneAtATime`), before writing a motion around it.
- **A recording's limbs on its floor lie under this body's floor.** Carried to this body, the sample's get-ups have the hands 9 to 15
  cm under the floor on all fours and a foot 10 to 19 cm under it as it steps (R2's step 10, `FSandboxMotionVariant::FitCm`): the
  skeletons' proportions and this body's shapes differ. The carry's springs never pull down, so the reference's lowered limbs gave
  nothing, the muscles pushed the body up off them, and what the world did push downward (its damping) was 0.14 of the body's weight
  (0.09 with the fit). A reference
  carried from another skeleton is checked against the floor where it touches it, not only where it ends.
- **A body the run lays down is "falling" to the controller at its first tick.** With R3's catch on (28 November), every `getup_roll_*`
  and `getup_drag_*` scenario failed: `body.laid_deg` puts the body down at 0 s, the controller entered Falling at 0.000 s and the catch
  reached out with the arms before the get-up. A behaviour of the fall runs only for a fall that began after the controller's first tick;
  a new behaviour of any state is run on the laid scenarios before it is believed.
- **A fall's way can turn round.** A body that sinks forward and then tips back (a tuning changed in the middle of `tune_live_*`) turned
  its sensed way by 180 degrees in 0.4 s; the catch followed it, its target swept past the shoulder and the elbow's target turned over.
  Whatever is aimed by the way is let go when the way turns far (`fall.catch_turn_deg`), not followed round.
- **The time to impact at a fall's start does not tell a blow from a shove.** The state Falling begins late (the pelvis tilted 35
  degrees or 20 cm down): 0.25 to 0.40 s are left for a blow of 200 N*s and for two fists through the chest alike, so "no time to catch"
  by that time curls all of them or none (R3's `fall.curl`). And the forward shove by hand reaches the floor 0.31 s after it becomes a
  fall: no arm of the falling body's strength gets forward in that time. What has to happen earlier belongs to the stagger, before the
  fall.
- **An arm that props a falling body is part of how it comes to rest.** Letting R3's catch go the moment the trunk touched (so that no arm
  stayed out under a lying body) kept the sideways knockdowns from rest for 6.3 s and for good: the near arm held the body on its side,
  and its sudden return to the fall pose rocked it. The catch is let go as the body lies.
- **A get-up clip run backward is not a body lying down.** R10's lowering (28 November) runs the clip back, led as it rose: from a
  standing rise the clip's first frames swing the legs up and hold the body a few centimetres off the floor on the world's pull, the body
  is "held up", and it drops when let go; and an attempt the matcher began late (0.8 to 0.97 s into its clip, a body sitting against a wall
  or the chair) ends its run back sitting, its chest 43 cm up, and the trunk falls (head 2.5 m/s) unless the world's pull is let go
  slowly; run back further, to the clip's own start, the clip's lying place was 40 cm off and the body was lost and dropped (3.1 m/s).
  Measure a reversed motion on the stand's late starts by furniture, not only on bodies laid in the open.
- **A rule of a limb's user meets every other user of that limb.** R2's held arm, answering a hand that took the forearm of a body R10
  lowered, turned the upper arm's target over by 159 degrees in a frame (`protect_phase_lowering_hold`); the C26 scenario of the new
  phase found it, the suite could not. The arbiter of limbs (`FSandboxLimbClaims`, R10's step 3) came after it; a new behaviour that
  moves the body under a held arm is still run with a hold in its phase.
- **A shortcut for the first attempt reaches every attempt after it unless it says otherwise.** The command `get_up` shortened the rest
  before a get-up to the reaction time; the scenarios made for it began one attempt and passed. On the stand of 150 a body by the chair
  failed, and the short rest sent it up again before it lay still, seven times; it never stood (with the usual rest it stood after
  three). A change to when a get-up begins is run on the stand, not on the scenarios that show it.
- **What a body does between its own parts does not move it against a drag.** R10's struggle (28 November) pulled the held arms in and
  dug the free heels in with muscles up to 3000 N*m/rad: the two hands, 300 N each, still dragged the body, which the floor holds back
  with at most its friction (about 540 N); only a yank that brings a grip to 450 N for 0.1 s frees it, and that took torque jumps of 600 to
  1450 N*m (C25's bound 439). Before a behaviour is built to beat a rule of the world (a grip, a drag), work out from the forces whether
  the body can at all.
- **A start blended from the body's own pose moves the target to where a displaced limb is.** The suite's largest target step (102 degrees,
  `command_get_up_held`) was read as two users of one arm; it was the start of a get-up 0.24 s after a hand let go of a forearm it had
  pulled away, the target stepping to where the arm was (no command jump with it). Read the command jump beside a target step before
  calling the step a defect.

## Hands, the grab and blows

- **The character's bodies do not answer the visibility channel** (the engine's `Ragdoll` profile ignores
  it). Aim traces go by object type (`ASandboxPawn::TraceAim`).
- **A static mesh component with the engine's basic sphere did not become a simulating body here** (valid
  body, `IsInstanceSimulatingPhysics` false; the cause was not chased). Hands are `USphereComponent`s with a
  mesh attached for looks.
- **A scripted hand goes to a point of the world and slides off a body that moves.** Of the two hands of
  `getup_held` one ends on the floor; "two hands only slow a get-up" was measured on one hand from part 6 on, and
  a threshold set between "one hand" and "two hands" from those runs separated nothing. A hand's sustained push
  also reads above its spring's cap (up to 342 N for 300 N: its weight and the body's push against it). Read
  `effectors.list.<hand>.touched_impulse_ns` before believing which hand pressed on what; a press of a known
  size is a scripted force (`impulse` with `duration_s`).
- **Two bodies that stand for one hand have other shapes, and one put where the other lies is inside what that
  one touched.** A ball where a plate lay flat on the chest is 3 cm in the chest. The hand changes its body only
  where the other has room (`WouldOverlap`), and a grip that closes on something is counted, not made a fist.
- **A hand has to be able to reach the speed a scenario asks for.** With the spring's cap of 300 N a target that
  goes 60 cm at 15 m/s is there and back before the hand has moved: no contact, no impulse, "no tunnelling". A
  sweep of speeds raises the cap (6000 N in M2 and here) and reports the speed the hand reached.
- **A soft part retreats from the hand that comes to hold it.** The head leans back on its neck from an
  arriving hand, and "the hand touched it a moment ago" was false when the grip closed. A grab asks what is
  within reach when the grip closes, not what was touched.
- **A hand jointed to one body still collides with its neighbours, and is caught between them.** Holding
  `spine_02` and pulled toward the head, the hand pressed on `spine_04` and its joint carried 556 N of a 300 N
  hand. A hand that holds something collides with no simulated body until it has let go and lies in none. When a
  joint carries more than the force that is put in, look for what else touches the body it is on.
- **A body that is pushed gives way, and is then out of the reach of the hand that pushed it.** A desktop hand
  that reached into the chest and closed its grip half a second later closed it on air: the body had leant back
  3 cm further than the reach. Close a grip while the hand still presses (`Tools\play.ps1 -Demo -Grab`).
  `palm_close_on_chest` closed its grip 0.45 s after the palm had pushed, and passed because the body swayed back into the
  hand in time; with the jumps' repairs it swayed back 0.1 s later and the grip closed on air. It closes while pressing now.
- **A test for "clear of everything" that leaves a whole actor out leaves out the other hand.** Two hands are
  one actor; a hand that had held something was set back to colliding while it lay inside the other one.
- **What a source decides about an effector belongs in its target.** The desktop's foot was made weak at rest
  by the pawn setting the effector's strength from outside: the recording did not have it, and a replay's foot
  had 1800 N where the session's had 40. The bootstrap's record-and-replay step caught it. The strength is a
  field of `FSandboxEffectorTarget` now, and a number of a recorded target.
- **A body that is made weak while it still moves keeps moving.** The foot came home from a kick at 8 m/s and
  was given its resting 40 N on the frame its target arrived: 40 N*s against 40 N. It keeps its strength for a
  settling time. The same for a target that jumps: a weak body drifts to it through what stands between.
- **A target that leaves at full speed at once makes a held stick trail, not strike.** The hand is thrown
  sideways at 130 m/s^2 and its wrist has 20 N*m: the stick turns back and is drawn past the body lengthwise.
  A swing with a thing in the hand gathers speed (`prop_stick_strike_arm`); the film showed it, the figures
  ("felt 0.7 N*s") did not say why.
- **A count of hits needs a rule for what one hit is.** Summed as the flinch sums and set back at each telling,
  a shove of 200 N*s over nine frames was nine hits; once a stretch of rising force, a hand held at the chest
  was six in six seconds. One contact is one hit, and a blow into a contact another (`FeelFrame`).
- **A hand's force cap is not what limits a punch; its spring is.** The spring asks for stiffness times the gap,
  and a target 15 cm inside a body is 450 N at 3000 N/m: raising the cap to 900, 1200 or 1500 N gave the same
  66 N*s. And a punch's impulse did not grow with its speed at all (50 N*s at 4 and at 8 m/s): it was the time
  the hand pressed. When a figure does not move with a setting, find what else bounds it before raising the
  setting further.
- **A rule that switches at one speed makes an edge of that speed.** A blow for every hand that arrives at
  2 m/s or faster turned a scripted push at 2.5 m/s into a punch (17 to 43 N*s, two steps). A blow's strength is
  graded from none at 2 m/s to all of it at 5.
- **A scripted impulse is felt at its body's centre of mass, and the chest's is at the solar plexus.** A zone
  for "the belly" that ended 1 cm above it made every scripted blow on the chest a hit into the belly: 20
  scenarios changed for a rule that was meant to touch three. Before a rule about where a hit lands is believed,
  list where the scripted hits of the suite land by it (`control.max_belly_hit_ns` above 0).
- **What a hand delivers is not what the body counts as a hit.** Of a punch's 112 N*s the body sums 63: what
  goes on pressing is pressure. A threshold on the body's side is set from the body's own figure.
- **A kick that looked right in scenarios was weak in the owner's hands, because the scenarios did not kick as
  the desktop does.** The scripted foot came at 28 degrees, the desktop's from a metre under the camera at 54
  (belly) and 65 (head): a blow from below that lifts a head and does not throw it back. A tool a person uses
  is measured on the path that tool makes (`FSandboxDesktopTargetSource::UpdateFoot`), not on one written
  beside it.
- **A head does not fall to momentum.** Kicked 30 cm through, the body fell from three points of nine on
  the head and not from the six beside them: the foot slides off a small round thing on a soft neck. What a hit on
  the head does is a rule on what the head felt (`reactions.knockdown_head_ns`), as for the belly.
- **A person's hand stops where it sees the surface; a scripted punch does not.** "A punch with weight behind
  it" had its target 15 cm inside the body for a tenth of a second and delivered 77 to 113 N*s; the owner's
  punches in the headset delivered 8 to 34 with the same hands. A strike that a person is to throw is measured
  on a target that stops at the surface and goes back at once (`Saved\Matrix\vr_punch_sweep`).
- **Something added to a target that is going back goes back with it.** A follow-through of 15 or 25 cm beyond
  the hand's target left a jab at 11 N*s to the digit: the target, with its speed backward in the spring's
  damping, took it along. And one held at where the fist landed made a hand that came to push stand still.
  The blow is a least depth along its own way (`USandboxEffectorComponent::Update`).
- **A probe written for a ball is wrong for a plate.** "The middle cannot be nearer a surface than half the
  thickness" holds flat on only; edge on, a plate 2 cm deep has not gone through anything.
- **A hand is one frame behind its target by design.** It is given the place for the start of the coming step
  and is there at the step's end: 2.2 cm at 2 m/s, 8.9 cm at 8 m/s, with the best velocity there is. A gap
  measured at the start of a step has that frame in it; do not tune it away with stiffness. Three times the
  force cap takes 0.2 cm off the gap at 8 m/s, a third less mass or twice the stiffness a centimetre
  (`Tools\swing-lag.ps1 -Tune`). "The cap holds the hand back from 6 m/s" was written from the force the spring
  law asked for and was wrong: a sweep is run before a cause is named.
- **A velocity differenced across a jump of the target is a blow.** 116 cm in a frame reads as 104 m/s; added to
  a speed limit it carried the target over the jump at once. Where a target may jump, the jump is found first
  and no velocity is made of it.
- **A test that strikes at a point of the world is written for the shape that was there.** The slap's path ended 4 cm inside the
  old head; the new, narrower head met it at the end of its path, at 1.4 m/s. And the plate of the open hand reaches 18 cm past the
  hand's own point: held with the fingers forward, it met the face with its rear edge. Change the body and these change with it.
- **An effector pressed at rest against the static world sleeps one substep in six.** The player's capsule held against a table
  read a contact force of 300, 300 and 150 N in turn at no speed: the island slept a substep in every six, with no contact in it
  (the pitfall of the solver's sleep in `CLAUDE.md`). The player's body never sleeps (`USandboxEffectorComponent::Start`); the
  hands are left as they were, and a hand's force held against the floor or a wall may read short the same way.
- **A spring's force cap does not bound what a collision delivers; the mass does.** The player's body of 70 kg on a spring capped
  at 300 N walked into the standing character at 1.4 m/s with 26 N*s into the chest and 43 into the belly over its press, and the
  character doubled over (the rule is made for a fist's blow at 40 N*s). At 10 kg (decision 28) the touch is 6 N*s. And a scenario's
  path that starts at full speed asks a body to accelerate faster than its cap lets it: it fell 23 cm behind and caught up at
  2.4 m/s; a path for a body that a person moves eases in as the person's control does (the stick's 0.2 s).
- **A grip that closes is a press, and a press is a hit by the body's own sum.** Scripted hands that came for the ankles at about 1 m/s and
  closed 1 cm into the foot summed 41 N*s on the leg, over R10's line of 40, and the leg was covered for 3 s (28 November;
  `getup_drag_ankles_crouch` since R10 was switched on, the struggle's ankle drags). A hand that comes to take hold comes slowly and
  closes on the surface; what a person's hand does there is to be seen in the headset.
- **"Open" for one user is not "open" for another.** The offered hand followed the player's hand while its grip was under 0.5; the hand
  closing to take it crossed 0.5 a tick before the grab did (0.4 to 0.6 for the effector), and the arm was let go just before the take.
  Once a hand is chosen, it is followed whatever its grip.

## The session, recordings and the headset

- **`-nohmd` does not stop the OpenXR plugin from creating an OpenXR instance at start-up.** With SteamVR
  as the runtime and no headset on, that call took 26 s per launch and ended in "Instance is not viable".
  So every tool launch passes `-nohmd -DisablePlugins=OpenXR` (`Invoke-Editor` in `Tools\common.ps1`); a VR
  run must leave both out. The plugin stays enabled in the `.uproject`.
- **Do not switch anti-aliasing back to TSR for VR.** At the headset's render target it cost 8.5 ms by
  itself and halved the frame rate. Renderer experiments go through `Tools\scenario.ps1 -VR -CVars "a=1,b=2"`,
  and every rendered run records the deciding console variables in `metrics.json` (`renderer`).
- **With a fixed timestep the engine does not limit the frame rate.** An interactive session would run as
  fast as the machine allows. `USandboxPlaySubsystem::HoldFrameRate` waits each frame out; it is the one
  place that reads the clock, and it must stay outside simulation code.
- **A pause set during a frame takes effect on the next one.** The pawn decides at the end of its tick
  whether the next frame steps. The engine also stops updating the camera of a paused game unless the
  player controller has `bShouldPerformFullTickWhenPaused`, and input actions need `bTriggerWhenPaused`.
- **A session and the scenario that replays it are two runs, not one.** A session's process runs no warm-up
  scenario, and its hands exist from its first frame and are made in another order relative to the body. A
  recording of the hands replayed as a scenario agreed with its session in every figure through the first contact
  and was a frame and 0.27 cm off after it. Hold a replay to its session by figures with limits
  (`Tools\record-replay.ps1`) and to another replay by hashes; "the same hashes as the session" was planned and
  could not be.
- **A recording's first frame belongs to one step of the body's life.** The pawn counts the frames the world
  stepped; the session knows on which of them its character was put into the world; the difference is
  `recording_start_s`. A reset during a recording makes a scenario that cannot end as the session did (the
  session says so in its log).
- **A camera locked to the headset is moved under its parent, so it must have one.** The engine sets the
  camera component's relative transform to the head's pose in tracking space every frame
  (`UCameraComponent::HandleXRCamera`). As the root of the pawn, the camera would carry the pawn to the head's
  place near the world's origin, and the hands' origin with it. The pawn's root is a plain scene component, the
  origin of the play space, and the camera hangs under it.
- **The engine hands a controller's angular velocity over as an axis times rad/s, already in its own axes and
  sense of rotation** (`FOpenXRHMD::GetPoseForTime` negates what OpenXR gives, because the change of axes is a
  reflection): `v = w x r` holds with `FVector::CrossProduct` as it is. Linear velocity and position are scaled
  by the world-to-metres value that is passed in (100 for cm). Read from the engine's source; step 4 in the
  headset is the first measurement.
- **Without a mapping context given to it before the session begins, the OpenXR plugin makes an action of every
  controller key** (`BuildLegacyActions`) and sends them as ordinary key events: that is how the squeeze reaches
  the player's input here (`FSandboxXRControllers::ReadSqueeze`). Such events go through the window's focus, as
  a keyboard's do, and the plugin logs a deprecation warning at every start.
- **OpenXR Toolkit (`XR_APILAYER_MBUCCHIA_toolkit`), an API layer installed on this machine, crashed the session
  in the headset 7 s after its start** (an access violation in its DLL, called from the D3D12 RHI), though M1's
  headset scenarios had run with it. `Invoke-Editor -VR` sets the variable its manifest names
  (`DISABLE_XR_APILAYER_MBUCCHIA_toolkit`) for that process. A crash in the headset whose stack starts in a DLL
  that is not the engine's: list the implicit layers (`HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit`).
- **A session that has logged its start has not shown that it runs.** The first launch in the headset crashed
  7 s in; the check looked for the start's line, found it, and the owner was told to put the headset on. Look at
  whether the process lives some seconds later (and for a session's own lines after the start).
- **Asked before the pawn has begun to play, the pawn refuses a recording without a word** (it has no hands yet).
  `-Record` from the first frame never recorded from step 1 until it was first used by a person, in the headset.
  The session starts it once the hands exist. A path that only a person takes is run once without one before a
  person's time is spent on it (`Tools\play.ps1 -Record x -Seconds 3`, then `record-replay.ps1 -Recorded x`).
- **The runtime's velocity of a controller is not the velocity of the pose it gives.** On PS VR2 through SteamVR
  the pose is predicted for the time the frame is shown at (about 47 ms ahead) and the velocity trails the motion
  of those poses by about 38 ms, smaller by a quarter. Measured on a recording, by fitting the recorded velocity
  to the difference of the recorded positions at a shift.
- **A controller out of the cameras' sight still has a "valid" pose.** It wandered a metre and a half in five
  seconds with a velocity of zero and jumped back 116 cm in one frame; the hand followed at 13 m/s. A pose that
  is not marked as tracked is followed for a moment only (`tools.headset.estimated_follow_s`, so that a mark
  dropped in a swing does not freeze a punch) and is a loss after that, and a target that jumps is not a motion:
  it is caught up with.
- **The grip of the PS VR2 controllers arrives as the Touch profile's grip axis and reads 0 or 1.**
- **Every second body put into a world that has had one is another body from its third frame on.** In a session
  of takes the pelvis of the first and the third fresh character stands 0.03 cm from where the second's and a
  scenario's stand, with nothing touching them, and standing makes half a centimetre of it within a second: a
  touch then lands a frame earlier and 2.6% harder. Not the hands (swapped, it stays with the take), not the
  first seconds of the process, not the order hands and character are made in. A take replays within limits,
  not to the digit; only a session's first body does that. And a shove that leaves the body 4 cm from a second
  step told two stories from that half centimetre: check a wiring with a touch, not with a shove.
- **A character put into the world in the middle of a frame has half a frame of its own.** The session's reset
  is carried out in the subsystem's tick, after the frame's physics, and a take begins with the next frame.
- **The engine makes a controller's vibration last one frame** (`FOpenXRInput::SetHapticFeedbackValues`: its
  duration is the frame's own), and hands the frequency on as hertz. A vibration that is to go on is asked for
  in every frame; asked for once when its strength changes, a held press is silent after 11 ms. Found by a
  reader of the code, not by a test: nothing here can feel a controller.
- **A session with hard blows is not reproduced by its replay.** `vr_check3` replayed ended 153 cm from where
  the session did, with none of its eight falls; `vr_check2`, with weak punches, to 0.00 cm. What happened in
  such a session is in the session's own log, not in `Tools\recording-report.ps1`, which reads a replay. Nor is one whose
  player's body shoves the character for seconds (`demo_walk`, 26 November): the body's first contact replays to the bone and
  the speed, the outcome 5.5 cm and a flinch apart; `Tools\record-replay.ps1 -Walk` judges the first and only shows the second.
- **A hand carried by a moving play space has the play space's velocity too.** The tracked source gave a hand's target the
  controller's velocity in tracking space alone; with the stick moving the play space at 1.4 m/s, the spring's damping (135 N*s/m)
  would have held every hand 6 cm behind a walk. The play space's velocity is added (`FSandboxPlaySpaceMotion`), and a snap turn,
  which carries a hand 15 to 26 cm in a frame (less than a hand moves in one, so not taken for a jump of the tracking), is told
  as a jump: the hand catches up with its new place instead of being flung through what is near at the speed of a blow.
- **A recording replayed with other hands measures nothing after its first hit.** The body stands elsewhere
  from then on and the recorded hands strike the air (9 hits of 51).
- **The HUD in the headset is one canvas across both eyes.** Text was cut between them and unreadable; the pawn's
  HUD draws nothing in a headset. Text for a person in the headset has to be a thing in the world.
- **A kept recording is of the code that made it.** `Recordings\demo_hands.json` was recorded before a hand had a
  grip and had 0 in every frame; a desktop hand has had a grip of 1 since. Nothing showed it, because an open
  hand that is only drawn collides as a fist does. When a recorded value gets a meaning, look at what the kept
  recordings have in it. And a recording carries what the hands were asked for, not what they could do: a
  session's replay scenario switches on the grab and the open hand's body if its session had them.
- **A zero can change its sign and nothing else.** With the camera under a root, the hands' recorded rotation has
  `0` where it had `-0`; the recording of the session without a person differs from the kept one in those
  characters only, and both replay to the same hashes. Compare recordings by a replay's hashes, not by bytes.
- **A session's hands fly into place before its recording begins, and a body that watches sees it.** The pawn makes the hands
  where it stands, and in the 0.7 s before the recording starts they go to where it starts; the replay puts them there at once.
  A body with gaze turned its head after that flight in the session and not in the replay, and the replay ended 2 cm off. A
  session that records shows the body its hands only while a recording runs. Two causes were believed first and ruled out by
  switching each (the hands seen before the replay's recording, a tuning change at 0 s); one behaviour on at a time found it.

## PowerShell, scripts and building

- **Keep the `AndroidFileServer` plugin disabled in the `.uproject`.** When it is enabled, the editor
  appends a section with a generated `SecurityToken` to `Config/DefaultEngine.ini` on first run, which
  must not be committed.
- **PowerShell variable names do not tell case apart, and a typed parameter converts what is assigned to
  it.** In a script with a `[string]$Metrics` parameter, `$metrics = Read-JsonFile ...` stores the text of
  the object, and every field of it reads as empty. Give locals names no parameter has.
- **Scripts target Windows PowerShell 5.1.** No `&&`, ternaries or null-coalescing.
- **Moving a file is not a way to take a job from a shared queue.** Windows renames through a handle; two
  processes can each open the file before either renames, both renames succeed, and neither process can then
  read "its" file. About one job in a thousand was run by nobody. A job is taken by writing a marker while a
  lock file is held open for writing (`UScenarioBatchSubsystem::TakeJob`), and a job without a result now prints
  `QUEUE WARNING` and keeps the processes' logs under `Saved\Queue`.
- **A process started with `UseShellExecute = $false` holds its starter's output open.** It inherits the handles of the shell
  that started it; a pool's engine process started that way kept the pipe of `pool.ps1 -Start *> file` open, and the call that
  redirected it returned only when the pool exited on its idle limit, ten minutes later (a build after it "hung" for as long).
  A process meant to outlive its starter is started through the shell (`Start-Process -WindowStyle Hidden`), its environment set
  on this process around the start (`Start-PoolProcess`).
- **PowerShell hash keys do not tell case apart either.** `@{ s = 0; S = 0 }` does not parse, and a script that
  does not parse fails at once: run a new helper once in the foreground before a long batch in the background.
- **A `TUniquePtr` to a type that is only declared does not compile in a `UCLASS`** (C4150 from the generated
  code); `UScenarioBatchSubsystem` owns its log device through a plain pointer.
- **A tool that checks only that its output file exists passes an old file as new.** `Tools\film-pair.ps1` looked for `film.mp4` in a
  folder that is the same on every run; a side whose run failed would have shown the last run's film under the new label, and said OK.
  Delete what a step is to make before it runs, and judge the step by its own verdict (found by the review of 27 November).
- **`@(, @(1, 2), @(3, 4))` is not a list of two pairs in PowerShell.** The comma binds to the first item only and wraps it
  once more; the first pair of every such row was never judged. And `Measure-Object` over the `$null` that an empty
  list becomes reads 0, which is within most limits: say what a figure that is missing means.
- **`git diff > file` in PowerShell 5.1 writes a patch `git apply` refuses.** The redirection writes UTF-8 with a byte-order mark and
  turns every line ending into CRLF; the blobs have LF, and every hunk "does not apply" (`--ignore-whitespace` applies it and turns whole
  files into changes). Write patches with `git diff --output=<file>` (R10, 28 November: the start's patch had to be normalised before the
  R10 diff could be made against a temporary index of the start, `GIT_INDEX_FILE`).
- **A file copied back keeps its old time, and the build calls the binary current.** After a patch was taken back, built, and put on
  again, the files were restored by `Copy-Item` from a backup: their times were older than the binary built from the reverted code, the
  build took 1 s, and the tests ran on the reverted code (62 tests instead of 63; a parser test failed on the new event). Touch restored
  files (`(Get-Item f).LastWriteTime = Get-Date`) and check the test count after such a build.
- **`git diff -- <files> --output=x` writes nothing.** After `--` the option is a path; the patch went to the screen and no file was made
  (28 November). Options before `--`: `git diff --output=x -- <files>`.
- **A new `.cpp` regroups the unity build, and names that two files declared quietly clash.** Two file-scope constants of
  `SandboxCarry.cpp` and `SandboxFingerRig.cpp` hid names of other files once a new file moved them into one group (C4459 as an error,
  28 November). Name file-scope constants after their file (`CarryGravityMps2`).
- **A film pair without `-Tag` is filed under its scenario's name, over any pair made of that scenario before.** The pair of decision 79
  (`protect_hit_rising_trunk`, `getup.strict_release` 0 and 1) wrote over decision 58's (the same scenario, R3 0 and A), which the
  documents still named (28 November, evening; made again). A pair that a decision or a page names is made with a `-Tag` of its own.
- **A switch a script takes and does not use says nothing.** `play.ps1 -Demo -Grab -Shot` ran the demo, printed `PLAY GRAB OK` and made
  no picture: `-Shot` rendered only the self-test (28 November). It refuses now. A new parameter is either used on every path that takes
  it or refused where it is not.
- **A sweep set that switches a value a scenario switches itself measures nothing new and fails `tuning_changes`.** `-Sets
  "behaviour.struggle=1"` on scenarios that set it by their own console event made every run fail; and `-Sets "|"` runs the files twice.

## Assets, the MetaHuman, maps and lighting

- **`/Engine/Tutorial/.../TutorialTPP` is not a usable ragdoll.** It explodes when all bodies simulate.
  Use the stock mannequin copied by `Tools\fetch-stock-content.ps1`
  (`/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple`).
- **Physics assets can contain non-rigid constraints.** The stock mannequin has one (`UserConstraint_0`).
  `FSandboxBodyProbe::MaxJointStretchCm` only measures joints with all linear axes locked.
- **Headless runs do not render, so nothing is "recently rendered".** Skeletal meshes must use
  `AlwaysTickPoseAndRefreshBones` and disable update-rate optimisation, as `ASandboxRagdollActor` does.
- **The stock MetaHuman physics asset cannot be simulated as it is.** Three defects, all fixed by the
  generator for `PA_SandboxNPC` and all still present in `PHYS_SandboxNPC` (scenario `drop_metahuman`):
  joint anchors sit 1.5 to 3.6 cm apart on all 15 joints, because they were placed for another body's
  proportions (M1 reported only the worst one, `spine_02`); every joint has an orientation motor that pulls
  it away from the reference pose; and the foot shapes do not lie flat on the floor. See `Specs/README.md`.
  A body that jumps or changes shape in its first frames has one of these; `metrics.json` shows the anchor
  gap and the spawn-pose angles per joint.
- **`FPhysicsAssetUtils::CreateNewBody` switches collision off between the new body and all earlier ones**
  unless `FPhysAssetCreateParams::bDisableCollisionsByDefault` is cleared. The generated NPC asset had every
  one of its 120 pairs off from M1 to M3: no self-collision, a forearm 17 cm inside the chest after a fall.
  The generator's log listed "colliding bodies" from its own plan, not from the asset. Read such a fact
  back from the asset: `penetration.self_collision_pairs` in `metrics.json` is what the run-time asset
  allows (127 for the NPC of 18 bodies, 144 since the head is a body of its own), and a metric that reads exactly 0 in every run is a metric to distrust.
- **Bodies that collide must not overlap in the reference pose.** They push each other apart from the first
  step: with the two thighs colliding (1.4 cm into each other) the standing character's feet turned at
  1.3 rad/s and the assist's load went from 0.05 to 0.22. Such pairs go under `collision.disable` in the
  spec; `penetration.spawn_self_cm` and the check `spawn_pose_clear_of_itself` find them.
- **The engine's joint axes: twist is the frame's X, swing1 its Z, swing2 its Y, and limits are symmetric
  around the parent-side frame.** An asymmetric range needs that frame turned to the middle of the range;
  `FPhysicsAssetGenerator::ComputeJointFrames` does it. The two swings are limited each by itself, as a box,
  not together as an ellipse: the engine's joint solver (the linear one, which every joint uses unless told
  otherwise) takes the turn apart into a twist and a swing and limits the swing's two angles separately
  (`FPBDJointCachedSolver::InitPyramidSwingConstraint`). The project believed in the ellipse from M2 to M3
  part 5, and added the collarbones for it.
- **A soft joint limit of the stock asset is not soft.** Stiffness 50 and damping 5 in the engine's
  acceleration mode hold a joint within 1 to 2.5 degrees of its range in a knockdown; a hard limit holds it
  no better and bounces (46 J gained against 26, rest after 4.0 s against 3.6). What a joint does at the end
  of its range is written in the spec (`"limit"`), not taken from a template without knowing.
- **Regenerating a map discards its baked lighting,** and the bake re-saves the map. So map generation is
  skipped while the map is newer than its spec, and the bake keeps a `.bake.json` stamp beside the map to
  know whether it is current. File times alone cannot tell.
- **The first lighting bake on a machine takes about 90 s, later ones 12 to 17 s** (Preview quality, the
  test room).
- **The MetaHuman's corrective blueprint runs on our body.** "No Animation Blueprints" was read as "none is there", and the
  plan said the skin had no correctives. The assembled mesh brings `ABP_Body_PostProcess` as its post-process blueprint and
  the engine runs it. It sees the pose the muscles are asked for, not the simulated body, and switching it off changes every
  hash by rounding. Ask the bones (`trace.json`, with the mesh's bone names from an editor script) before saying what moves them.
- **Editor Python needs `PythonScriptPlugin` enabled in the `.uproject`.** Without it `-ExecutePythonScript`
  is silently ignored and the editor just sits open.
- **MetaHuman needs the engine option "MetaHuman Creator Core Data"** (Epic Games Launcher, engine tile,
  Options; about 6 GB). Without it the log says "Optional Content folder not found", skin textures are not
  synthesised and the assembled character is incomplete.
- **MetaHuman assembly raises script errors that are not failures.** `build_meta_human` throws "Cannot break
  link ..." for Control Rig graphs in the engine's own face assets even when the assembly succeeds; the
  script judges the build by the assets it produced and reports the message as a warning.
- **A MetaHuman is two skeletal meshes** (body and face) plus a Blueprint that joins them. Loading only the
  body mesh gives a headless character.
- **MetaHuman scripting.** `Tools\metahuman.ps1` runs `Tools\python\metahuman_build.py` in the full editor
  (not a commandlet). The `rig` and `textures` steps call Epic's cloud; the first run opens a browser for an
  Epic sign-in, which a person must complete, and later runs reuse the stored sign-in. Never pass account
  credentials on the command line. The rig state is not exposed to Python; `can_build_meta_human()` is true
  only when the character is both rigged and textured.

<!-- removed: getup.foot_wait_s getup.pelvis_follow_s getup.frame_follow_s balance.ankle_side_gain -->
<!-- not-names: vr_check2 vr_check3 -->
