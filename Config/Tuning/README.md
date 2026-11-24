# Tuning

Every number that shapes how the body behaves lives in the `*.json` files of this directory. The code holds
no default values: a value missing here stops the body from starting, and a name the code does not know is
an error, so a misspelt one cannot pass unnoticed.

A value's name is its path in the file. In `body.json`

```json
{ "muscles": { "legs": { "stiffness_nm_per_rad": 800 } } }
```

is `muscles.legs.stiffness_nm_per_rad`. That name is used everywhere: here, in `-Tune`, in the console and
in `metrics.json`. Several files are merged into one set of names; a name may be defined only once. There
are four: `body.json` (the character), `poses.json` (the poses the character's muscles hold),
`effectors.json` (the player's hands) and `tools.json` (the desktop pawn's tools).

## Values of the body (`body.json`)

| Name | Unit | Meaning |
| --- | --- | --- |
| `rig.pelvis`, `rig.chest`, `rig.head`, `rig.foot_left`, `rig.foot_right` | bone name | Bodies the balance assist holds. Text, read when the body starts; cannot change in a running body |
| `muscles.<group>.stiffness_nm_per_rad` | N·m/rad | Spring of every joint in the group toward the target pose. 0 switches the muscles off |
| `muscles.<group>.damping_s` | s | Damping as a time: damping torque = stiffness × this × angular velocity |
| `muscles.<group>.max_torque_nm` | N·m | Largest torque of a muscle; 0 = no cap. From published peak torques of an adult man: legs 250 (knee and hip extension), spine 300 (trunk extension), arms 80 (shoulder and elbow), neck 50. One figure for a group, so a wrist may ask what an elbow can give. With the caps the push matrix passes as without (160 of 160); they only act in a stagger, a step or a fall |
| `muscles.<group>.pose_velocity_share` | 0..1 | How much of the target pose's own turning speed the damping works against. 0: the damping resists all motion, and a pose that jumps is not a blow (at Physics Control's default of 1 a jump of the leg's target cost 600 J) |
| `assist.<body>.horizontal_n_per_m`, `vertical_n_per_m` | N/m | Pull of the body toward its standing position |
| `assist.<body>.angular_nm_per_rad` | N·m/rad | Turn of the body toward its standing orientation |
| `assist.<body>.damping_s` | s | Damping of the three assist springs, as a time |
| `assist.<body>.max_torque_nm` | N·m | Largest torque of the assist's turn. 0 = no cap |
| `assist.<body>.max_force_n` | N | Largest pull of the assist along each axis. 0 = no cap. Set for the get-up (`states.get_up`), where the pull has to help and not carry |
| `getup.rest_s` | s | A body that has lain still this long gets up. 0: it stays down. A scenario's body stays down whatever this says, unless the scenario has `"get_up": true` in its `body` |
| `getup.clip_back`, `clip_front`, `clip_left`, `clip_right` | asset paths | The clips to get up by: recorded get-ups that end standing in place, here the four of Epic's Game Animation Sample (`Tools\fetch-sample-anims.ps1`). The one whose pelvis lies at its start most like the body's is taken, whatever its name. Read when a body first gets up |
| `getup.ease_s` | s | Time over which the muscles and the assist's pull come up from nothing as the get-up begins (the body was limp a moment before; the clip's first pose is asked for at once and whole) |
| `getup.rate` | | How fast the clip's time runs while the body keeps up: 1 as recorded |
| `getup.end_s` | s | The time in a clip by which its body stands (2.6 s; the rest of the sample's 5 s is standing still) |
| `getup.lag_cm`, `side_lag_cm` | cm | The clip's time waits while the pelvis is further than `lag_cm` below or above where the clip has it, or further than `side_lag_cm` from it over the floor. A body that is slower than the recording, or held down, is waited for. Sideways the tolerance is wider: where on the floor a get-up in place happens matters little, and a table's leg may be in the way. 16 and 20: on the stand of 50 falls 48 to 49 stand with them over three seeds, 45 to 48 with 12 and 12 |
| `getup.foot_lag_cm`, `foot_wait_s` | cm, s | ...or a foot that is off the floor is further than `foot_lag_cm` from where the clip has it, but for no longer than `foot_wait_s` at a time: a foot that does not come (a leg across the other, or against a chair) is left where it is. Waited for without end, such a foot kept the clip at its first key through six attempts |
| `getup.frame_follow_s` | s | The clip is laid over the feet the body stands on: its root follows the feet that carry weight to where they are from where the clip has them, with this lag when they carry all of the body's weight, more slowly when less. Without it a foot the body stood on could not go where the clip stepped it to, and the pelvis was pulled over the clip's feet instead of the body's own |
| `getup.stall_s` | s | A get-up whose clip has stood still this long is given up: the body lets go, lies, and tries again after `rest_s`, by another clip if another starts at all like the body lies (the same clip from the same place fails the same way) |
| `getup.lost_cm` | cm | A pelvis this far from where the clip has it has been thrown off: the get-up is given up at once. 40. It goes with the lags above and the assist's caps: with a pelvis cap of 500 N and 45 here the assist brought back a body that a 200 N*s blow had moved 40 cm; with the cap at 250 N the blow fells the body at any value from 30 to 45, and at 30 a 30 N*s push did too once the clip ran 16 to 20 cm ahead |
| `getup.upright_deg` | degrees | The body stands again once the clip is through and its pelvis is within this of upright, with both feet on the floor |
| `solver.position_iterations`, `velocity_iterations`, `projection_iterations` | count | Solver effort on the character's bodies. Whole numbers, at most 255 |
| `thresholds.flinch_impulse_ns` | N·s | A hit of at least this impulse makes a standing body flinch |
| `thresholds.flinch_window_s` | s | Impulses that arrive within this time of each other count as one hit (a push delivered over several frames) |
| `thresholds.fall_pelvis_drop_cm` | cm | The posture is lost, whatever the capture point says, once the pelvis is this far below its standing height (the legs gave way) |
| `thresholds.fall_tilt_deg` | degrees | ...or tilted this far from its standing orientation |
| `thresholds.rest_speed_mps`, `rest_angular_speed_rads` | m/s, rad/s | A fallen body lies (and lets its muscles go, `states.lying`) once it moves and turns no faster than this as a whole (root mean square over its bodies, weighted by mass). Not "motionless": held until it was, the fall's protective pose kept the body curled on the floor and it took 3.5 s to come to rest instead of 1.7 |
| `thresholds.rest_time_s` | s | ...for this long |
| `thresholds.impact_let_go_s` | s | A falling body lets its muscles go (`states.lying`) this long after its trunk or head first met the floor, at rest or not: the pose of a fall is for meeting the floor. Held until the body was at rest, the pose kept it a stiff shape (knocked over by the head, it stood on its head with its legs up for a second; on its back its forearms stayed in the air). 0: only once it is at rest |
| `thresholds.held_time_s`, `held_descent_cm` | s, cm | A body that is not upright, has touched nothing of the world for the first and has not come down by the second (its centre of mass below the lowest it has been) is held up, by a hand or a hook: it lets go of its fall's pose and hangs in its stance's, with the falling body's muscles, until it touches something or comes down again. 0 s: never |
| `balance.foot_contact_min_n` | N | A foot stands when the floor pushes up on it with at least this force, averaged over a frame. In quiet standing each foot carries about 390 N |
| `balance.sole_tolerance_cm` | cm | The sole of a standing foot is the part of its shapes within this of its lowest point: what the support polygon is the hull of |
| `balance.recoverable_cm` | cm | How far outside the support the capture point may be and still be brought back by the ankles and hips. Further out, a step is needed |
| `balance.step_reach_cm` | cm | How far beyond the support's edge a step can put a foot... |
| `balance.step_time_s` | s | ...and how long a step takes. The capture point runs away by e^(omega t), omega = sqrt(g / centre-of-mass height) (2.2 times over 0.25 s). A body whose capture point would be beyond the reach by the time a step lands does not give up: it steps at once, as far as it can, and again, and falls when its posture gives way (`thresholds.fall_*`). Only a body whose legs are placed from outside (`foot_target`) is counted as fallen there |
| `balance.decision_s` | s | A step is needed only once the capture point has been past `recoverable_cm` this long |
| `balance.replant_air_s` | s | A foot that the floor has not touched for this long is planted anew where it lands: its assist anchor goes there, and the anchors of pelvis, chest and head follow the new support |
| `balance.lift_grace_s` | s | A foot knocked off the floor still counts as support, where it is, for this long: it comes back down by itself, which is not a step |
| `balance.anchor_follow_s` | s | Time over which the anchors of pelvis, chest and head go over to a new support, eased in and out |
| `balance.ankle_gain` | factor | Ankle strategy, forward and back: the centre of pressure moves toward the capture point by this times its error (the error against where the centre of mass stood at the start, over the feet). The ankles' targets turn by the torque that takes over the weight's stiffness. 0 switches it off |
| `balance.ankle_side_gain` | factor | The same sideways. 0: on this body a sideways turn of the ankles after a blow lifted the unloaded foot, whichever way it turned (measured in M3 part 3) |
| `balance.ankle_integral_s` | s | The ankles' slow part: a steady offset of the pelvis from its assist anchor moves the centre of pressure by the offset once per this time, so that the ankles, not the assist, hold the posture's sag. 0 switches it off |
| `balance.ankle_integral_max_cm` | cm | The slow part's largest shift of the centre of pressure |
| `balance.ankle_max_deg` | degrees | Largest turn of the ankles' targets |
| `balance.hip_deg_per_cm` | degrees/cm | Hip strategy: while the capture point is outside the support, the trunk bends toward it at the hips (forward or back), by this per cm outside. 0 switches it off |
| `balance.hip_max_deg` | degrees | ...up to this |
| `balance.response_s` | s | Time constant with which the strategies' turns and the lean follow what they are asked for, also on the way out when the body falls: no target jumps |
| `balance.lean_max_cm` | cm | Under a held push (a force felt in every frame of `reactions.pressure_s`) the centre of mass leans F h / (m g) toward the force, the assist's anchors with it: at most this far. The capture point a decision is made on moves the other way by the same amount |
| `balance.recover_s`, `recover_speed_mps` | s, m/s | A staggering body stands again once its centre of mass has been over the support, the capture point within `recoverable_cm`, both feet down and the body slower than `recover_speed_mps`, for `recover_s` |
| `balance.step_margin_cm` | cm | A recovery step puts the foot where the capture point will be when it lands, this much further along its way |
| `balance.step_margin_goal_cm` | cm | ...and where that would leave the capture point less than this far inside the new support, at the nearest point that does (up to 15 cm away). 0 aims at the point alone |
| `balance.step_lift_cm`, `step_press_cm` | cm | How high the swinging foot is lifted, and how far below the planted height its swing ends (so that the leg puts the foot on the floor, not next to it in the air) |
| `balance.step_over_share`, `step_descent_share` | 0..1 | Share of the swing by which the foot's point is over its target, and the share from which it comes down. A touch of the floor ends the swing from the earlier of the two |
| `balance.step_settle_s` | s | A foot that landed on its toe or an edge is let down onto its sole over this long; while it settles, a decision counts its whole sole as support |
| `balance.swing_stiffness_scale`, `swing_damping_scale` | factor | The swinging leg's muscles against the legs' group (eased in over `step_ease_s`), so that the foot keeps up with its point |
| `balance.swing_pose_velocity_share` | 0..1 | The swinging leg's `pose_velocity_share`: its target is the swing's own smooth path |
| `balance.swing_foot_n_per_m` | N/m | An assist spring on the swinging foot along its swing. 0 leaves the swing to the muscles |
| `balance.step_width_min_cm` | cm | A step that keeps to its own side lands at least this far to that side of the other foot |
| `balance.cross_offset_cm`, `cross_past_cm` | cm | A step across the other foot passes and lands this far ahead of it (or behind), and no further past it sideways than `cross_past_cm` |
| `balance.step_ease_s` | s | Time over which a step's placement of the legs eases in |
| `balance.stance_follow` | 0..1 | While a foot swings, the standing leg is solved for a pelvis this far from its anchor toward where the body is: 0 holds the body back, 1 lets it travel |
| `balance.stagger_damping_s` | s | ...and behind that by the body's speed times this, so that the planted legs brake the body |
| `balance.leg_extension_max` | 0..1 | Planted legs are solved with the pelvis low enough for each leg to reach its foot at this share of its length |
| `balance.lower_s`, `lower_max_cm` | s, cm | Lag of that lowering, and its limit: a pelvis further down than this is a fall, not a wide stance |
| `balance.anchor_room_share` | 0..1 | The anchors glide to a new support no faster than keeps the capture point within this share of the way to the support's edge. 0: no limit |
| `balance.step_shift_share`, `step_shift_ready_cm`, `step_shift_s`, `step_shift_max_s` | 0..1, cm, s, s | A step that was asked for (not a recovery step) first moves the weight onto the other foot: how far the pelvis's goal goes toward it, how near the capture point has to be to that sole for the foot to lift, the lag, and after how long it lifts anyway |
| `balance.step_asked_time_s` | s | How long the swing of an asked step takes |
| `balance.regroup_wait_s` | s | After recovery steps, a body that has stood still this long brings a foot back to its place beside the other (0: the feet stay where the steps put them) |
| `balance.regroup_tolerance_cm`, `regroup_max_steps` | cm, count | Feet this near their stance are left alone; at most this many regrouping steps after one loss of balance |
| `balance.regroup_inside_share` | share | Before the foot lifts the body goes to the other foot: to the middle of its sole, less this share of the way to where the lifted foot will stand (0 the middle of the stance sole, 0.5 the middle of the stance to come). On one leg the body then leans toward the foot that is coming, not away from it |
| `balance.regroup_lead_gain`, `regroup_shift_gain_per_s`, `regroup_lead_max_cm` | -, 1/s, cm | The pelvis's goal is moved beyond that point while the capture point is not there: by the distance still to go times the gain, plus that distance times the second value for every second it stays, by no more than the third. The legs' frames follow the body, so a goal set once is not reached |
| `balance.regroup_margin_cm` | cm | The foot lifts when the capture point, where it will be as the foot lands (`step_asked_time_s`), is this far inside the support the step gives |
| `balance.regroup_shift_max_s` | s | A regrouping step whose weight shift has not got that far by then is given up; the feet stay where they are |
| `balance.arm_dead_cm`, `arm_full_cm` | cm | The arms join the fight for balance (`poses.balance_*`) once the capture point is further than the first from where the body stands, in full at the second. Quiet standing and the idle's sway stay inside the dead zone. `arm_full_cm` at or below `arm_dead_cm`: the arms take no part |
| `balance.arm_response_s` | s | Lag with which the arms follow what is asked of them, up and down again |
| `reactions.flinch_weakening` | fraction | Stiffness of the hit region's muscles at the moment of a hit, as a fraction of normal |
| `reactions.flinch_recovery_s` | s | Time over which they return to full strength, in a straight line; also how long a flinch lasts |
| `reactions.flinch_pose_full_ns` | N·s | A hit of this impulse puts the flinch's pose offset on at full weight, a smaller one in proportion. The offset fades over `flinch_recovery_s` from the hit, whatever state the body is in by then, and is gone once it lies |
| `reactions.flinch_give_deg` | degrees | How far the struck body's own joint gives way along the hit at full weight |
| `reactions.pressure_s` | s | What the body feels is split in two: pressure, the felt force smoothed over this time, and the hit, the impulse beyond it. Only a hit counts toward `thresholds.flinch_impulse_ns`, so a hand that keeps pressing is felt as it lands and not again |
| `reactions.held_push_s` | s | A force felt in every frame for this long is a held push, which the body leans into (`balance.lean_max_cm`) and which moves the capture point a decision is made on. A shove of 0.1 s is not one: its momentum is in the capture point already (with a window of 0.1 s, a 2000 N shove of 0.1 s counted as held, the body leant into it as it fell, and lying it gained 3.2 J) |

## Poses of the body (`poses.json`)

The muscles turn each body toward a target pose. That pose is written here as joint angles and evaluated
by the body's animation instance (`USandboxAnimInstance`, no Animation Blueprint).

| Name | Unit | Meaning |
| --- | --- | --- |
| `poses.blend_s` | s | Time over which the target goes from one pose to the next, eased in and out |
| `poses.<pose>.<joint>.swing1_deg`, `swing2_deg`, `twist_deg` | degrees | How far the joint is turned from the reference pose, about its own axes |
| `poses.stance.*` | | The standing pose: the reference pose with the arms let down to the sides (upper arms -27 and -5). All zeros is the reference pose itself (arms held out), which is what the body held before it had poses |
| `poses.fall_blend_s` | s | Time over which the target goes over to the pose of a fall; placed feet are let go over the same time |
| `poses.fall_back.*`, `poses.fall_forward.*`, `poses.fall_side.*` | | What a falling body does with itself, chosen as the fall starts from the way the pelvis leans and the body moves: backward it sits down and curls up, chin on the chest, hands behind; forward the hands go out in front and the knees bend; sideways (written for a fall to the left, mirrored for the right) the arm of that side reaches for the floor. Kept as the target while the body lies, with the lying state's weak muscles |
| `poses.balance_back.*`, `poses.balance_forward.*`, `poses.balance_side.*` | | What the arms do while the body fights for its balance, added on top of the pose in proportion to how far the capture point is from where the body stands (`balance.arm_*`): going over backward the arms come forward and up, going over forward they go back and out, going over to the left (mirrored for the right) the far arm goes up and out. An arm thrown the way the body tips turns the trunk back the other way |
| `poses.idle.animation` | asset path | The clip whose motion a standing body carries on top of its stance: breathing, the shoulders, a slow sway (the engine's `MM_Idle`, a loop of 7.6 s). Only what each bone does relative to the clip's first frame is taken, for trunk, neck and arms; the clip's pose and its legs are not. Read when the body starts. Empty: no idle |
| `poses.idle.weight` | | How much of the clip's turns the body carries: 1 as recorded (hands travel 4 to 7 cm, the chest 2), 0 none |
| `poses.idle.sway_weight` | | How much of the clip's sway of the pelvis (0.8 cm) the whole body leans by, about its ankles. With both weights 0 a run is bit for bit what it was before the body had an idle |
| `poses.flinch.*` | | Added on top of the pose during a flinch, at full weight for a hit of `reactions.flinch_pose_full_ns`: the shoulders rise (collarbones +12), the head draws in (neck -10), the chest bends a little (-4) and the elbows close (+15) |

Joints are named by their child bone: `spine_02`, `spine_04`, `neck_01`, `clavicle_<side>`, `upperarm_<side>`,
`lowerarm_<side>`, `hand_<side>`, `thigh_<side>`, `calf_<side>`, `foot_<side>`, with `_l` and `_r`. The axes
are the ones `Specs/README.md` gives each joint's limits in, so the same numbers mean the same thing in a
pose and in a limit: for the spine and the hip, swing1 positive bends backward and forward respectively as
the spec's ranges say (spine -22.5 is forward, hip +100 is forward); for the knee and the elbow positive
bends further; for a collarbone positive lifts the shoulder; for the neck positive tilts the head back. A
mirrored joint (the right side) moves as the mirror image for the same numbers. Angles are counted from the
reference pose, including for the elbow, whose reference pose is already bent by about 37 degrees (its spec
range counts from the straight arm). A pose is not checked against the joint limits; a run reports any
violation (`max_joint_limit_violation_deg`).

A pose can be changed while a body runs, like any value: `Sandbox.Tune poses.stance.spine_02.swing1_deg=-10`
leans the standing body forward over `poses.blend_s` (scenario `pose_lean`).

The `balance` values decide what the body senses of its own balance (which feet stand, the support polygon,
the capture point; `Scenarios/README.md`, "Balance, energy and penetration") and what it does about it
(M3 part 3): the capture point decides whether the body stands, needs a step or falls, the ankles and the
hips bring it back, the assist's anchors follow the feet, and a held push is leant into. The support a
decision is made on is every foot the floor touches at all (or touched within `lift_grace_s`), not only
those that carry `foot_contact_min_n`: a foot resting lightly on the floor takes weight the moment the
body sways onto it. A body that needs a step staggers and steps (M3 part 4); only one whose legs are
placed from outside is held by the assist, and its result file says so (`control.balance.step_needed_frames`).

Groups are `spine`, `neck`, `arms`, `legs`; the collarbones belong to `spine` (the shoulder girdle is held by the
back: with the arms' soft muscles they sagged 10 degrees under the arms). Assist bodies are `pelvis`, `chest`, `head`, `feet`. The ranges
that are accepted are in `FSandboxBodyTuning::Validate`; they catch a slipped decimal point, not physics.
The conversion from N·m/rad to engine units is taken from the engine source and has not been calibrated by
measurement (see `docs/testing.md`).

## Values of the player's hands (`effectors.json`)

A hand is a rigid sphere with real mass, pulled toward its target pose by a spring. It is never moved
directly. The same values apply to the desktop pawn's hands and to the hands of a scenario.

| Name | Unit | Meaning |
| --- | --- | --- |
| `effectors.hand.mass_kg` | kg | Mass of the body: the hand and the part of the arm that moves with it. What a strike carries is this times its speed |
| `effectors.hand.radius_cm` | cm | Radius of the collision sphere. Read when the hand is created; cannot change in a running hand |
| `effectors.hand.stiffness_n_per_m` | N/m | Pull toward the target position. With the force cap it sets how far behind its target a blocked hand can be before it pulls with all it has: cap / stiffness, 10 cm at the present values |
| `effectors.hand.damping_s` | s | Damping as a time: damping force = stiffness × this × (velocity of the target − velocity of the hand). 0.045 s is about critical for 1.5 kg on 3000 N/m |
| `effectors.hand.max_force_n` | N | The player's strength: the largest force the spring applies, in any direction. Must be above 0; a hand without a cap would be the kinematic hand the design rules out |
| `effectors.hand.angular_stiffness_nm_per_rad`, `angular_damping_s`, `max_torque_nm` | N·m/rad, s, N·m | The same for the hand's orientation. The torque cap is per axis |
| `effectors.hand.snap_distance_cm` | cm | A hand further than this from its target is put on the target, at rest: the way back for a hand left behind a wall. Not physics; a scenario in which it happens fails |

No gravity acts on a hand: the arm that would carry it is not simulated, and the force cap is not spent on
the hand's own weight. The force of a **contact** can exceed the cap for a moment, because a moving hand
that is stopped gives up its momentum as well; the cap limits what the spring adds.

## Values of the desktop tools (`tools.json`)

| Name | Unit | Meaning |
| --- | --- | --- |
| `tools.hands.rest_forward_cm`, `rest_side_cm`, `rest_down_cm` | cm | Where a resting hand is held, relative to the camera |
| `tools.hands.reach_cm` | cm | How far straight ahead of the camera a reaching hand's target goes. The wheel changes it in a session |
| `tools.hands.reach_speed_mps` | m/s | How fast that target travels out and back |
| `tools.impulse.impulse_ns` | N·s | What one click of the impulse tool applies. The wheel changes it in a session |
| `tools.impulse.wheel_factor` | factor | One notch of the wheel multiplies or divides the impulse by this |
| `tools.drag.stiffness_n_per_m`, `damping_s`, `max_force_n` | N/m, s, N | The drag handle's spring between the point that was clicked and the point the player moves |
| `tools.wheel_distance_factor` | factor | One notch of the wheel multiplies or divides a distance by this: a hand's reach, the length of the drag handle |
| `tools.camera.speed_mps`, `fast_factor` | m/s, factor | Speed of the free camera, and how much faster it is with the fast key |
| `tools.camera.look_deg_per_count` | degrees | Turn of the camera per count of mouse movement |
| `tools.time.slow_motion_divisor` | count | In slow motion the world steps on one frame in this many. A whole number from 2 |

The ranges that are accepted are in `FSandboxInteractionTuning::Validate`. These values have no states: a
value under `states.<state>.effectors...` is accepted by the store and read by nobody.

## Values per state

The section `states` holds values that apply only while the body is in one state. Everything a state does
not set comes from the base values above.

```json
{
  "muscles": { "legs": { "stiffness_nm_per_rad": 800, "damping_s": 0.06, "max_torque_nm": 0 } },
  "states": {
    "falling": { "muscles": { "legs": { "stiffness_nm_per_rad": 100, "damping_s": 0.3 } } }
  }
}
```

The names are `states.falling.muscles.legs.stiffness_nm_per_rad` and so on. Known states are `standing`,
`flinch`, `stagger`, `falling`, `lying` and `get_up`. `states.get_up` holds what the body gets up with: stronger
arms, and an assist that pulls the pelvis and the feet toward where the clip has them, each with a cap on its
force (250 N and 50 N: the lowest of a sweep with which the body gets up from all four sides).

| State | The body is | Leaves when |
| --- | --- | --- |
| `standing` | Upright on the base values | A hit reaches `thresholds.flinch_impulse_ns`; or the capture point is beyond a step's reach (`balance.step_reach_cm`, `step_time_s`); or the pelvis sinks or tilts past `thresholds.fall_pelvis_drop_cm`, `fall_tilt_deg` |
| `flinch` | Upright, the muscles of the hit region (spine, neck, arms or legs) weakened and recovering | `reactions.flinch_recovery_s` has passed; or, as from `standing`, balance or posture is lost |
| `falling` | Let go: no balance assist, weak muscles with strong damping | Its trunk or head met the floor `thresholds.impact_let_go_s` ago, or it has been at rest for `thresholds.rest_time_s` |
| `lying` | Down and limp | Not yet (getting up is M3) |

A state does not inherit from another state, only from the base values: `lying` has to switch the assist
off itself, although `falling` did so before it. The body's checks make sure every state that sets values
gives a usable tuning, and a unit test that `falling` and `lying` have no assist left.

## Changing values

| How | Lasts | Example |
| --- | --- | --- |
| Edit a file here | Until the next edit. A running session picks it up within 30 frames | Save `body.json` |
| Console | Until reset or the end of the session | `Sandbox.Tune muscles.legs.stiffness_nm_per_rad=400,solver.position_iterations=32` |
| Command line | The whole run | `Tools\scenario.ps1 -Name stand_npc -Tune "muscles.legs.stiffness_nm_per_rad=400"` |
| Another directory | The whole run | `Tools\scenario.ps1 -Name stand_npc -TuningDir Saved\MyTuning` |

Console commands: `Sandbox.Tune name=value[,name=value...]`, `Sandbox.TuneReset [name ...]` (no names: all),
`Sandbox.TuneList [part of a name]` (values in force and where each comes from) and `Sandbox.TuneReload`
(read the files now).

Overrides lie on top of the files: reloading a file keeps them. With a state, the state's own value wins
over the base value, and in both places an override wins over the file. So an override of
`muscles.legs.stiffness_nm_per_rad` does not reach a state that sets its own; override
`states.falling.muscles.legs.stiffness_nm_per_rad` for that.

Every change is applied as a whole or not at all. A file that does not parse, has an unknown or missing
name or a value out of range is not applied: the log says why and the previous values stay in force. The
same goes for an override; the console prints the reason. A file caught half-written by an editor's save
is rejected the same way and read again when the save completes.

## How it works

`FSandboxParamStore` (module `SandboxCore`) reads the files, keeps the overrides and counts revisions.
`FSandboxBodyTuning::DeclareParams` tells it the body's names, states and range checks, and
`FSandboxInteractionTuning::DeclareParams` those of the hands and tools; each module does so when it is
loaded. `USandboxBodyController`, `ASandboxEffectorRig` and `ASandboxPawn` compare the revision once per
frame and re-read their values when it moved.

The files of this directory together must hold every declared name, whichever module declared it. A test
that reads the directory therefore takes its schema from the process-wide store
(`FSandboxParamStore::Get().CloneSchema()`), not from one module.

Hot reload compares the files' size and modification time every 30 frames. It counts frames and never reads
the clock, and with unchanged files it does nothing, so it cannot disturb a scenario's repeatability. In a
scenario the runner does the looking, on fixed frames of the run, so that an edit made by the scenario
itself lands on the same frame every time.
