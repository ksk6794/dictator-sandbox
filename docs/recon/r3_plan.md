# R3: the fall as the references drive it, every item

**Outcome (28 November, the night):** every item built behind its own switch at 0, every hash of `r3base` kept (`docs/testing.md`,
"R3"). Proposed for the owner's look: variant A (a, b, c, f), and B (A and `fall.blend`); d and e measured and not proposed. Not met at
the gate, after two independent readings and their fixes: C16 by hand for A, and on 96 more falls C14 by hand and the rest within 3 s
after three blows of 160 N*s (the forward shove by hand; slow tips). Changes to this plan made on the way: the catch
blended by its joints' turns with its own elbow pole and its anatomical hinge, a catch begun only while the placement can come in, let go when the
way turns 60 degrees and carried with the chest as it is let go; the head's turn on `neck_01` (the gaze's joint), not on `head`; the
curl also by the blow (`fall.curl_from_ns`) and the sit also by the legs' give (`fall.sit_leg_stiffness`); the scenarios as below.
Decisions 54 to 58 (`docs/active-ragdoll.md`, section 10). The plan as written follows.

Written 27 November 2026, 22:30, at the start of the owner's autonomous window (about 22:10 to 07:00), by the owner's decision 54
(`docs/active-ragdoll.md`, section 10: R3 now, stage 2 of the faster checks after it). R3's first item (the way and the time, a measure,
`FSandboxFallSense`) and the first half of its second (the reaching poses blended over the arms by the way, `fall.blend`, kept at 0 by
the owner, decision 37) were built in the night of 26 to 27 November (`docs/recon/r3_step1_plan.md`, `r3_step2_plan.md`). This plan is
for the rest, in the owner's order. If the context of the work is cut, it goes on from here and from `docs/testing.md`, "R3".

Read before it (`docs/pitfalls.md`): **Falls and the get-up** (all: the pose of a fall is for meeting the floor; a pose recorded for a
body in the air is not one for a body that lies; the muscles of a falling body do work; muscles that chase a point of the world while the
body turns do work; a fall's legs let go in time; lying must not wait for motionless); **Muscles, poses and the standing body** (a target
that jumps puts energy in; a point fixed in the world is a moving target for a body that turns; a leg's two-bone placement does not place
an arm; a hand placed near its own shoulder turns the elbow over; a change to a path several features share is measured on each);
**Measuring, runs and tests** (a measure can be met by what it was not meant for; check a limit against today's body first; a tuning file
edited while runs go is read by them); **Solver** (a torque put on a body is not a muscle's torque; the engine caps a drive axis by axis;
Physics Control damps a muscle against the target's turning speed). And in `docs/testing.md` by the mechanism's words (reach, arm, neck,
head, brace, yield, sit, curl, turn): "N6 step 4 in part", "N6 step 5", "N6, the reach on today's body", "R3, step 1", "R3, step 2".

What those say, for this plan: the head kept off by the neck alone (120 N*m/rad, more flexion) struck in 33% of the falls by hand (75%),
cost the stand two falls and one scenario; braced arms sent to world points gained up to 83 J in falls backward (behind the hips) and
kept falls from rest for up to 4.2 s; more damping on them cut the energy but not the rest; the sample's reaching poses over the whole
body held a leg up as the body lay, over the arms alone they put a hand first in all eight knockdowns but the head struck harder by blows
and the stand lost 10 first attempts. So: nothing chases a point of the world; the arms' targets are in the trunk's frame; their strength
comes up only near the floor and goes at the touch; the legs keep the written poses unless an item changes them; every item is measured on
the stand of three seeds, because the bodies lie otherwise and the get-up is chosen from how they lie.

## Rules for every item

- Each item behind its own switch in `Config/Tuning/body.json` (and `VisitValues`), 0 in the file: every hash of the baseline `r3base`
  (the files of 27 November evening, R2's rest on). A switch that is 1 only by `-Tune` until the owner has seen its video.
- Measured switch by switch (`-Tune`, one change at a time), then together; on: the suite's changed hashes named and explained, the
  knockdowns by blow and by hand, the scorecard's fall rows (`natural.ps1 -Quick`), the push matrix, the stand on three seeds
  (`getup-seeds.ps1`), C25 against 439 N*m.
- Every item that would be switched on gets a pair of videos (`film-pair.ps1`, switch 0 against 1, the same camera and times): falls
  forward, back and sideways, by blows of 30 to 200 N*s and by hand.
- Shared paths measured on every caller: the arms' tone (`UpdateTone`) is shared with N6's reach and the guard; the hand target
  (`PlaceArm`, `FSandboxLimbTarget::bArm`) with the guard, the held arm and N6; the fall's base pose (`ApplyPose`) with the get-up, which
  starts from how the body lies; the neck's turns (`UpdatePoseTurns`) with the gaze and the flinch.

## The items

### a. The arms' strength on the time to impact (`fall.arm_curve`)

The sample's `Ragdoll_UpdatePhysicsStrengthsFromCurves`: while the body falls the arms' tone is a curve of the sensed time to impact
(`FSandboxFallSense::TimeToImpactS`), not a constant. Far from the floor (more than `fall.arm_brace_from_s`) the arms are loose
(`fall.arm_far_stiffness`, a factor of the falling arms' 40 N*m/rad); as the floor comes they brace (`fall.arm_brace_stiffness`,
`_damping`); the moment a hand or a forearm touches the world they give way (`fall.arm_touch_stiffness`, `_damping`: soft and damped, so
that the arm takes speed out of the fall and does not throw the body back). Every change goes through the tone's follow (`tone.rise_s`,
`tone.fall_s`): no strength jumps. Lying, the tone goes back. With `fall.blend` it is decision 37's next step; it works on whatever pose
the arms are asked for (the written fall pose, the blend, the catch).

- Classes: `USandboxBodyController::UpdateTone` (the arms' goal while falling), `FallArmGoal` (the curve, pure), the hand touch kept for R3
  in `OnBodyHit` (`bFallHandTouchFrame`, separate from N6's).
- Switch and values: `fall.arm_curve` 0; `fall.arm_brace_from_s`, `fall.arm_far_stiffness`, `fall.arm_far_damping`,
  `fall.arm_brace_stiffness`, `fall.arm_brace_damping`, `fall.arm_touch_stiffness`, `fall.arm_touch_damping`.
- Measures: the knockdowns by blow and by hand (C14 to C17), the energy, the rest; with `fall.blend` 1 and 0.
- Risks: braced arms do work (N6's 83 J): the brace only near the floor; the touch's softness keeps a hand from snapping (30 rad/s).

### b. The catch fall (`fall.catch`)

The arms are placed in the trunk's frame toward where the floor will be, at low stiffness with damping and a torque cap, and they give
way when a hand touches; they do not chase a point of the world (N6's mistake).

- Where the floor will be: at the fall's first sensed tick, and following the sensed way over `fall.catch_follow_s`, the trunk's
  orientation at impact is foreseen as the chest's orientation now tipped along the way until it leans `fall.catch_tilt_deg` from
  upright. The floor's direction from each shoulder at that moment (down, and `fall.catch_ahead` along the way) is taken into the
  foreseen chest's frame and held there: the hand's target is that direction from the shoulder at `fall.catch_reach` of the arm's
  length, in the chest's frame, so the arm is asked for the pose relative to the trunk that it will need at impact, from the start,
  and the target moves with the trunk, never with the world. Once a hand touches, its arm's target stays where it is in the trunk's
  frame and the arm yields (item a's touch tone, or its own `fall.catch_touch_*` if item a is off).
- The placement: `PlaceArm` (the two-bone placement of an arm, the elbow back and out, the shoulder and the elbow held to their ranges),
  its weight eased in over `fall.catch_blend_s`, out over `poses.fall_blend_s` as the body lies or the fall ends; the guard's arm is left
  to the guard. Arms' torque cap while catching: `fall.catch_max_torque_scale` of the arms' 80 N*m.
- Falling backward, the hands go behind the hips (people do; N6 left them out because braced arms swinging back gained 83 J): kept, with
  the low tone, and measured; `fall.catch_back` 0 leaves the back fall's arms to the written pose if it costs the energy.
- Classes: `UpdateFallCatch`, `StopFallCatch`, `FSandboxFallCatch` (the foreseen tilt and the floor's direction in the chest's frame,
  pure arithmetic, unit-tested), `FFallRun` (what a fall keeps: its start, its sensed time at the start, the way, each hand's target in
  the chest's frame, each hand's touch).
- Switch and values: `fall.catch` 0; `fall.catch_tilt_deg`, `fall.catch_ahead`, `fall.catch_reach`, `fall.catch_follow_s`,
  `fall.catch_hold_s` (the target is held once the time to impact is under this), `fall.catch_blend_s`, `fall.catch_back`,
  `fall.catch_max_torque_scale`, `fall.catch_touch_stiffness`, `fall.catch_touch_damping`.
- Measures: C14 (a hand ahead of the chest first), C16 (the head's speed), the hand's speed at the floor and its joints' speed (30 rad/s),
  the energy (40 J), the rest (3 s), `metrics.pose_prior` in the fall; the stand on three seeds (the bodies lie with their arms elsewhere).
- Risks: the arms under the body as it lies (the get-up's choice): they are let go over the fall pose's blend once the trunk touches;
  the elbow turned over near the shoulder (`docs/pitfalls.md`): the reach is a share of the arm's length, never near the shoulder; the
  target of an arm whose shoulder is out of range: held to the range.

### c. The head kept off the floor (`fall.head`)

The neck braced and turned away from the floor: chin to the chest going backward, the head back going forward, the ear to the upper
shoulder going sideways. The written fall poses already turn `neck_01` this way; at the falling neck's 40 N*m/rad they are not held
(`docs/testing.md`, N6 step 4). Here: the neck's tone braced while falling (`fall.head_stiffness`, `fall.head_damping`, factors), and a
turn of the head's own joint (`head`, which no fall pose touches) by the sensed way every tick (`poses.fall_head_back`, `_forward`,
`_side`, mirrored for the right), weighted as the blend's weights are (the squares of the way's parts in the body's frame), following over
`fall.head_follow_s`; both go as the body lies (over `poses.fall_blend_s` and the tone's fall).

- Classes: `UpdatePoseTurns` (the turn), `UpdateTone` (the neck's goal), `FallWayWeights` (the weights from the sensed way, shared with the
  blend, made into one function).
- Switch and values: `fall.head` 0; `fall.head_stiffness`, `fall.head_damping`, `fall.head_follow_s`; the three poses in `poses.json`.
- Measures: C15 and C16 by blow and by hand, the neck's speed, the stand (`room_movable_fall_gets_up` failed with N6 step 4's values).

### d. Curling up when there is no time to catch (`fall.curl`)

A blow of 200 N*s takes the body to the floor sooner than the arms can come: knees and arms in, the back rounded, the chin to the chest.
At the fall's first sensed tick, if the time to impact is under `fall.curl_below_s`, the fall's base pose is `poses.fall_curl` (hips and
knees flexed, the spine rounded, the elbows bent with the forearms before the chest and the face, the chin in) for the whole fall, and
the catch is not made. Decided once, at the start (no switching back and forth); a fall that has the time catches.

- Classes: `ApplyPose` (the curl's pose), `FFallRun::bCurl`.
- Switch and values: `fall.curl` 0; `fall.curl_below_s`; `poses.fall_curl`.
- Measures: the time to impact at the fall's first tick on the knockdowns (blows) and by hand, to set `fall.curl_below_s` between them
  (read first); C15 and C16 by blows; C14 by blows will fall where the body curls (said, not hidden).
- Risks: a body that lies curled gets up worse (the matcher's clips begin from a body lying flat): the curl is let go as the body lies,
  as the fall pose is.

### e. Sitting down going backward, turning to a side when there is time (`fall.sit`, `fall.turn`)

- Sitting down: a fall backward (the back weight over `fall.sit_from`) folds the hips and the knees early and rounds the back, so that the
  pelvis meets the floor before the trunk (the human figure: an impact 18% slower). The written `fall_back` has the legs' fold; what this
  adds is a turn on top that comes in faster than the fall pose (`poses.fall_sit`, over `fall.sit_blend_s`).
- Turning to a side: a fall backward or forward with at least `fall.turn_min_s` to the impact at its start twists the trunk toward a side
  (`poses.fall_turn`, written for a turn to the left, mirrored), so that the body lands on a hip and a shoulder with the hand of that side;
  the side is the way's sideways part, or the trunk's turning speed when the way has none.
- Classes: `UpdatePoseTurns` (both as turns), `FFallRun::TurnSide`.
- Switches and values: `fall.sit` 0, `fall.sit_from`, `fall.sit_blend_s`; `fall.turn` 0, `fall.turn_min_s`; `poses.fall_sit`,
  `poses.fall_turn`.
- Measures: the pelvis first in backward falls (`first_touch_bone`), the pelvis's and the head's speed at the floor; C14 to C17.

### f. The fingers spread, the palms to the floor (`fall.palms`)

The fingers are already `splayed` in a fall since decision 53 (`UpdateFingers`). What is left is the palm: with the catch, the wrist is
turned so that the palm faces the floor's direction at impact (the hand's target orientation in the chest's frame, through the placement's
end, as a foot is held level), measured as the angle between the palm's normal and the floor's at the hand's first touch
(`control.fall.palm_to_floor_deg`, new, read only).

- Switch and values: `fall.palms` 0; `fall.palm_share` (how much of the turn the wrist takes, within its range).
- Measures: the angle at the touch; the wrist's speed and limit excess at the touch; the fingers' hashes (bones only).

## Scenarios (C26 and the items)

- `fall_catch_forward`, `fall_catch_side`, `fall_back_r3` (blows of 200 N*s on the chest, forward, to the right and backward; the
  chest's 80 and 120 N*s do not fell the body; R3 on by a console event at 0 s): a hand first ahead of the chest (forward and sideways),
  the head not struck, no target step over 110 degrees, rest within 3 s, 40 J. (Written with 80 N*s blows and a `fall_catch_back` at
  first; changed after the independent reading of 28 November: a fall backward is not caught with `catch_back` 0.)
- C26, the new phase is the catch (the arms out toward the floor): `fall_phase_catch_hit` (a second blow while the arms reach),
  `fall_phase_catch_push` (a push from the side as it goes down), `fall_phase_catch_hold` (a hand that follows the forearm takes hold of
  it as it falls). Each asks: no target jump over the suite's limit, rest within 3 s, 40 J, the joints within 2 cm of stretch, and the
  hold that the grip lasts and the body is not thrown.

## The gate (the owner's list of 27 November)

- C14 to C17 by blows (`knockdowns.ps1`) and by hand (`knockdowns.ps1 -ByHand`); the scorecard's fall rows (`natural.ps1 -Quick`).
- A fall rests within 3 s and gains at most 40 J.
- The three scenarios of C26.
- Decision 7: the push matrix, the knockdowns and the stand on three seeds worse only with the reason written and a video.
- C25 no higher than 439 N*m (`natural.ps1 -Quick`).
- Every figure of the gate measured after the last change, at the gate.

## If an item sticks

What was found is written in `docs/testing.md`, and the next item of R3 that needs nothing of the owner is taken; once R3 is done or
stuck, the next work of section 8 that needs nothing of the owner.
