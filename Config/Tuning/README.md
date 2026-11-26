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
| `rig.head_if_own` | bone name | In an asset with a head of its own on top of the body `rig.head` names (`PA_SandboxNPC` since 26 November), the body that carries the head; empty: `rig.head` is the head whatever the asset has |
| `muscles.<group>.stiffness_nm_per_rad` | N·m/rad | Spring of every joint in the group toward the target pose. 0 switches the muscles off |
| `muscles.<group>.damping_s` | s | Damping as a time: damping torque = stiffness × this × angular velocity |
| `muscles.<group>.max_torque_nm` | N·m | Largest torque of a muscle; 0 = no cap. From published peak torques of an adult man: legs 250 (knee and hip extension), spine 300 (trunk extension), arms 80 (shoulder and elbow), neck 50. One figure for a group, so a wrist may ask what an elbow can give. With the caps the push matrix passes as without (160 of 160); they only act in a stagger, a step or a fall |
| `muscles.<group>.pose_velocity_share` | 0..1 | How much of the target pose's own turning speed the damping works against. 0: the damping resists all motion, and a pose that jumps is not a blow (at Physics Control's default of 1 a jump of the leg's target cost 600 J) |
| `muscles.<group>.weight_share` | 0..1.5 | How much of the torque that carries the weight of what hangs beyond the joint the muscle gives on top of its spring (`FSandboxWeightTorques`). 0: none, the spring carries it by its error; 1: all of it. Each group's share follows its value over `weight_follow_s`, so it never jumps with a state |
| `muscles.joints.<kind>.stiffness_scale`, `damping_scale`, `max_torque_scale` | factor | A kind of joint's own muscle (both sides alike: `spine_02`, `spine_04`, `neck_01`, `head`, `clavicle`, `upperarm`, `lowerarm`, `hand`, `thigh`, `calf`, `foot`, `ball`), as factors on its group's stiffness, damping and cap, in every state. 1: the group's muscle (N3, step 1) |
| `tone.<level>.<group>.stiffness`, `damping` | factor | The body's tone: for each level (`limp`, `relaxed`, `ready`, `braced`) and group a factor on stiffness and damping, on top of the state's values. All 1 today (`ready` is the muscle as it is) |
| `tone.standing.<group>` | level | The level a group stands at with nothing happening: 0 limp, 1 relaxed, 2 ready, 3 braced. A scenario's `tone` event asks the whole body for a level for a time |
| `tone.brace_from_ns`, `brace_delay_s`, `brace_hold_s` | N*s, s, s | A hit of at least this braces the whole body that much after it (a reflex) for that long; 0 never |
| `tone.daze_from_ns`, `daze_scale`, `daze_s` | N*s, factor, s | A hit on the head from this much up to the knockdown's dazes the body: legs and spine at down to `daze_scale` of their tone, graded with the hit, back over `daze_s`; 0 never |
| `perception.enabled` | 0 or 1 | The body judges what moves near it (N5, step 1: it judges and does nothing). A scenario's body only with `"perception": true` in its `body`; a session's not yet |
| `perception.view_half_angle_deg`, `perception.notice_s` | deg, s | In view: within this angle of where the head faces; noticed: this long in view |
| `perception.threat_speed_mps`, `threat_time_s`, `threat_near_cm` | m/s, s, cm | A threat: something noticed that closes on the head or the trunk at least this fast with at most this long to the contact, or is this near the face |
| `perception.head_radius_cm`, `trunk_radius_cm`, `thing_radius_cm` | cm | The sizes the contact is reckoned with; the last for a thing told with no size |
| `perception.gaze` | 0 or 1 | 1: the head turns toward what the body attends to (a threat, else the nearest thing it noticed that moves), through the neck's target (N5 step 2); 0: not |
| `perception.gaze_head_share`, `gaze_speed_dps`, `gaze_max_yaw_deg`, `gaze_max_pitch_deg` | share, deg/s, deg | How much of the angle the head takes (the eyes the rest, in M5), how fast it turns, how far to a side and up or down |
| `perception.gaze_dwell_s`, `gaze_move_mps` | s, m/s | How long it stays on a thing before another may take its place (a threat always may); how fast a thing must move to be looked at |
| `perception.anticipate` | 0 or 1 | 1: a threat the body noticed braces it (the tone's braced level, the whole body) and draws its head in (the flinch's pose) before the contact (N5 step 4); 0: not |
| `perception.react_s`, `anticipate_hold_s`, `anticipate_pose_s` | s | How long after the threat the anticipation begins (a person's reaction to what is seen), how long it holds, how fast its pose comes in and goes |
| `perception.guard` | 0 or 1 | 1: a threat to the head with time to spare, or held near the face, raises the near arm into its way (N5 step 5); 0: not |
| `perception.guard_time_s`, `guard_reach_cm`, `guard_hold_s`, `guard_blend_s` | s, cm, s, s | The least time to the contact for a guard; how far from the face the hand goes toward the threat; how long it holds; how fast it comes and goes |
| `perception.gaze_rise_s` | s | How long the head takes to come up to its turning speed, and down from it: the neck's target never starts or stops turning in one frame |
| `fall.aim` | 0 or 1 | 1: while the body falls the hands reach for the floor along the way it goes (N6); 0: today's fall, bit for bit |
| `fall.reach_ahead_cm`, `fall.hand_height_cm` | cm | Where a hand is sent: this far from its shoulder along the fall's way, its wrist this high above the floor |
| `fall.reach_blend_s`, `fall.way_follow_s` | s | How fast the reach comes in; how fast the fall's way follows the body |
| `fall.reach_back` | 0 or 1 | 1: the hands reach in a fall backward too, behind the hips; 0: only forward and sideways |
| `fall.arm_reach_stiffness`, `arm_reach_damping`, `arm_yield_stiffness`, `arm_yield_damping` | factors | The arms' tone while they reach, and once a hand has touched the floor (they give way); by the tone's own times |
| `tone.rise_s`, `tone.fall_s` | s | Time constants of a tone going up (bracing) and coming down (letting go): no strength jumps |
| `balance.ankle_as_torque` | 0 or 1 | 1: the ankle strategy's shift of the centre of pressure is a shift of where the floor's push is taken to be for the legs' weight torques (bounded by the soles), not a turn of the ankles' targets. Needs `muscles.weight_legs_by` 1 and the legs' `weight_share` |
| `assist.internal_share` | 0..1 | How much of what the assist wants of the pelvis, the chest and the head of an upright body the legs and the trunk give instead of the world: its horizontal force and upright torque become a push of the floor at a shifted place, bounded by the soles and by friction; the trunk's joints carry the chest's and the head's torque. What is cut off is not given back to the world. Needs `muscles.weight_legs_by` 1 |
| `muscles.weight_follow_s` | s | Smoothing of what the world pushes the bodies with, and of how much of the weight the world holds up (a body in free fall carries none) |
| `muscles.weight_legs_by` | 0 or 1 | Where the push under a standing foot comes from: 0 what the floor pushed it with (the torque then follows the push it makes, which integrates the spring's error), 1 a share of the weight by where the centre of mass lies between the soles, only while the body is upright |
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
| `getup.pelvis_follow_s` | s | Where no foot carries the body (it lies, rolls over, is on hands and knees) the clip is laid over the body itself: its root follows the pelvis over the floor with this lag, less by the share of the weight the feet carry. 0: it does not. A body that turns into a clip's first pose moves 20 to 35 cm over the floor; a clip that stayed where it was put waited for the assist to drag the pelvis back, which is what half the assist's force went into. With 0.3 the stand of 50 falls is as good at an assist share of 0.27 as it was at 0.51 (M3 part 7). A blow moves the pelvis faster than this follows and is still told by `lost_cm`: a blow fells a body getting up from 140 N*s on, with and without it |
| `getup.hold_force_n` | N | A body pressed down with this much or more does not push on: its clip waits and the attempt is given up after `stall_s`. What presses is the force other simulated bodies (hands) put on the body in every frame of `reactions.held_push_s`. 450: one hand at its cap of 300 N (`effectors.hand.max_force_n`) is felt as up to 340 N and slows a get-up, two hands (600 N) keep the body down (owner's rule). 0: the body always pushes on |
| `getup.pressed_follow`, `pressed_lag_cm` | share, cm | What a body does that is pressed with less than `hold_force_n`: it gets up where the hand lets it. The clip goes on following the pelvis when the feet carry the body too (by `pressed_follow`: 0 not at all, 1 as much as it follows the feet), and may run further ahead in height (`pressed_lag_cm` in place of `lag_cm`), since muscles are springs and a body that pushes against a hand sags under it. Without the two, one hand on the pelvis stopped a get-up in its crouch as surely as two. Neither acts on a body nothing presses |
| `getup.stall_s` | s | A get-up whose clip has stood still this long is given up: the body lets go, lies, and tries again after `rest_s`, by another clip if another starts at all like the body lies (the same clip from the same place fails the same way) |
| `getup.wait_s` | s | ...and so is one whose clip has waited this long in all, counting only the time in which nothing presses on the body (a body that a hand slows is not hurried). 0: no such bound. A clip that moves on by a frame now and then never stands still for `stall_s`: under the table's edge a body held one pose for 13 s of an attempt of 15.6 s. 5: on three seeds of the stand no attempt lasts longer than 7.7 s (15.6 without), 148 of 150 falls end standing (149), 126 at the first attempt (127), 1.36 attempts a fall (1.33); the fall it costs is at the table, where a long wait sometimes ended with the body backing out. 4 gives the same counts with 1.38 attempts; 3 costs first attempts (116) |
| `getup.lost_cm` | cm | A pelvis this far from where the clip has it has been thrown off: the get-up is given up at once. 40. It goes with the lags above and the assist's caps: with a pelvis cap of 500 N and 45 here the assist brought back a body that a 200 N*s blow had moved 40 cm; with the cap at 250 N the blow fells the body at any value from 30 to 45, and at 30 a 30 N*s push did too once the clip ran 16 to 20 cm ahead |
| `getup.upright_deg` | degrees | The body stands again once the clip is through and its pelvis is within this of upright, with both feet on the floor |
| `getup.rest_per_ns_s`, `getup.rest_max_s` | s per N*s, s | N7: the body lies `getup.rest_s` plus this per newton-second of the hardest hit felt from a second before its fall, at most `rest_max_s` (0: no bound). 0: `rest_s` for every fall |
| `getup.gather_s`, `getup.gather_rate` | s, share | N7: while the clip waits for the body its time goes back by up to `gather_s` and comes again, at `gather_rate` of its rate, never past where the wait began: the body gathers itself instead of freezing. 0: the clip stands still |
| `getup.rise`, `getup.rise_clip_back`, `getup.rise_at_back_s`, `getup.rise_s`, `getup.rise_com_cm` | 0 or 1, path, s, s, cm | N7, a get-up made for this body: from the back the body follows `rise_clip_back` (a get-up to a crouch) only to its crouch at `rise_at_back_s`, then rises by itself over `rise_s`: muscles from the crouch to the stance, the pelvis's assist up to the standing height and upright, the centre of mass kept over the middle of the feet; it waits while the pelvis lags or the centre of mass is more than `rise_com_cm` outside the feet. 0: the clip to its end |
| `getup.rise_lean_s`, `getup.rise_lean_max_deg` | s, degrees | While it rises (`getup.rise`), the body moves its own centre of mass over the feet as the ankle strategy does standing: everything above the feet leans toward where it should be, by the error over the centre of mass's height per `rise_lean_s`, at most `rise_lean_max_deg`; after the rise the lean fades over `poses.blend_s` |
| `getup.fast_rise`, `getup.fast_hand_cm`, `getup.fast_force_n`, `getup.fast_torque_nm`, `getup.fast_ease_s` | 0 or 1, cm, N, N*m, s | N7, the rise as fast as the recording: from the clip time at which the clip's hands leave the floor for good (the last time either is lower than `fast_hand_cm`) the clip does not wait for the pelvis or a foot, and the pelvis's assist may pull with up to `fast_force_n` and turn with up to `fast_torque_nm`, eased in over `fast_ease_s`. 0: the clip waits all the way |
| `getup.end_one_foot` | 0 or 1 | N7, the recording carried: 1, at the clip's end one foot on the floor is enough to stand (upright within `upright_deg`); 0, both feet. Carried hard, the pelvis was held at the clip's height and the other leg did not reach the floor |
| `getup.carry`, `getup.carry_n_per_m_per_kg`, `getup.carry_nm_per_rad_per_kg`, `getup.carry_damping_s`, `getup.carry_max_weights`, `getup.carry_max_up_weights`, `getup.carry_max_nm_per_kg`, `getup.carry_release_s`, `getup.carry_yield_ns` | 0 or 1, N/m per kg, N*m/rad per kg, s, weights, weights, N*m per kg, s, N*s | The led get-up (`docs/active-ragdoll.md`, R1): 1, the world pulls every body of the character toward where the clip has it (`FSandboxCarry`: a spring per body, per kg of it, capped at `carry_max_weights` of the body's own weight over the floor, at `carry_max_up_weights` upward, never pushing down, and at `carry_max_nm_per_kg`), the clip runs at its rate without waiting and is not laid over the feet, and the get-up's assist on pelvis and feet is off. The springs come up with `getup.ease_s` and go over `carry_release_s` after the get-up; a hit felt of `carry_yield_ns` or more lets the body go at once (0: never). What the springs give is counted in the assist's share. 0: the get-up as it was, bit for bit |
| `getup.reached_feet` | 0 or 1 | N7: 1 aims the feet's assist, and measures a foot's lag, at where the foot is with the legs' joints as the muscles are asked for them (held to their ranges, on the mesh's proportions), not where the clip's own foot is (which the body's foot stayed 17 to 18 cm from while the clip waited for it). 0: the clip's own foot |
| `getup.progress_cm`, `getup.progress_window_s`, `getup.progress_pelvis` | cm, s, 0 or 1 | N7: above 0 the clip waits for a foot in the air (with `progress_pelvis` 1 for the pelvis too) only while it comes this much closer in every window; one that does not come is not waited for until it is within its bound again (the clip had asked for where the body cannot reach and stood until `foot_wait_s` ran out). 0: as before |
| `getup.slow_from`, `getup.rate_follow_s` | share, s | N7: above 0 the clip slows as the body falls behind, from this share of the lags that stop it (`lag_cm`, `side_lag_cm`, `foot_lag_cm`) to a stop at the whole of them, and runs while the body eases in; each change of its speed is smoothed over `rate_follow_s`. 0, 0: the clip runs or stands still, as before |
| `solver.position_iterations`, `velocity_iterations`, `projection_iterations` | count | Solver effort on the character's bodies. Whole numbers, at most 255 |
| `surface.friction` | coefficient | Friction of the body's surface, every body but the feet: skin and cloth. 0.3, which is 0.5 on the floor: the middle of what is given for cloth and skin, and from 0.4 to 0.7 no run tells a difference; at 0.9 the body gets up at the first attempt 101 times of 150 instead of 124 to 130 (it has to slide to turn over and come onto hands and knees). The controller makes a physical material of its own and gives it to its bodies; a passive body keeps the engine's default (0.7, 0.3). A contact's friction is worked out of the two surfaces that meet, by `friction_combine`: against the engine's default surface (0.7), which the floor and the hands have, the average, so 0.3 here is 0.5 on the floor. `Tools\surface-friction.ps1` measures what the floor really gives by dragging the lying body |
| `surface.feet_friction` | coefficient | The same for the two feet: the soles. 0.7, on the floor too. At 1.1 (0.9 on the floor, and 1.1 of one foot against the other) the stand was no better, a blow on the thigh tripped the body, and after a push sideways it stood frozen on crossed legs |
| `surface.restitution` | 0 to 1 | 0.1: a body hardly bounces. Standing, falls and get-ups are the same within noise from 0 to 0.3. How much of the speed a body meets a surface with it leaves with, combined with the other surface's (0.3 by default) by `restitution_combine`. The engine applies it only to a contact that closes faster than 5.6 cm/s (1000 cm/s² times the 1/180 s substep) |
| `surface.friction_combine`, `restitution_combine` | 0 to 3 | How the two surfaces of a contact are combined: 0 the average, 1 the smaller, 2 the product, 3 the larger. Of the two surfaces' modes the engine takes the one with the higher number, and everything else in the world asks for 0. With 1, the value here is the contact's for any surface that has more (a body that does not bounce does not bounce off anything) |
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
| `balance.lift_smooth`, `balance.settle_smooth`, `balance.reach_smooth`, `balance.release_smooth` | 0 or 1 | The jumps of a step put right, each by itself (docs/natural-body.md, decision 13); 0, as they are: the leg's target jumps. Lift: the lifting leg's frame goes over from the one it was last solved in, the foot's level and its ankle's limit come in over `step_ease_s`. Settle: a landed foot settles from the height its leg was solved for. Reach: `leg_extension_max` comes in with the placement's weight. Release: a leg let go as a fall begins goes back to the pose from the target it was solved for |
| `balance.stagger_brake_follow_s` | s | Time constant over which the brake of a stagger on the legs frame (`stagger_damping_s` times the speed of the body) follows the speed, in and out; 0, as it was: at once, which moved the legs frame by 15 to 40 cm in the frame after a blow |
| `balance.lift_frame_s` | s | Time over which a lifting leg's frame goes over from the upright one it stood in to the body as it is; 0: `step_ease_s`. After a blow the two are 25 degrees apart |
| `balance.land_ease_s`, `balance.fall_ease_s` | s | Over this time the swinging leg's own strength goes once it has landed, and the muscles go over to the strengths of a fall as it begins; 0, as they are: at once |
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
| `balance.rest_step_s` | s | A staggering body that has come to rest (both feet down, slower than `recover_speed_mps`) with its centre of mass and its capture point outside the part of its soles that touches the floor, for this long, needs a step: at rest nothing brings it back over its feet, and only the stagger's upright hold on the pelvis keeps it up. In a recovery that goes well the condition lasts 0.09 s at most. 0 switches the rule off (the body then stays as it is, in Stagger, for good) |
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
| `reactions.double_over_ns` | N·s | A hit from the front into the belly or the solar plexus that brings this much beyond the pressure felt already, summed over one contact, makes an upright body double over (40: a jab is 22 to 26, a push 24 to 28, a punch with weight behind it 53 to 64, a kick 80 and more). 0: never |
| `reactions.double_over_s`, `double_over_in_s`, `double_over_out_s` | s | The doubled-over pose is held until this long after the hit (2.0), comes in over 0.2 and goes out over 0.8, by its own clock in every state; a body that lies or gets up lets it go |
| `reactions.double_over_from`, `double_over_to` | share | The belly, as a stretch of the line from the pelvis's joint (0) to the neck's (1): -0.15 to 0.53, the groin to just under the chest's centre of mass (where a scripted blow "on the chest" is felt) |
| `reactions.reflex_delay_s` | s | After a hit felt while standing the body moves by mechanics alone for this long (N4): the flinch and its weakening, a doubling over, the balance strategies' new goals and a decision to step wait for it; a fall by the posture does not. 0: at once |
| `reactions.knockdown_head_ns` | N·s | A hit on the head that brings this much beyond the pressure felt already, summed over one contact, takes the legs from under an upright body: it falls and gets up as after any fall (35: a punch brings 27 at most, a scripted 30 N·s blow 30, a kick 35 to 76). 0: never |
| `reactions.flinch_give_deg` | degrees | How far the struck body's own joint gives way along the hit at full weight |
| `reactions.pressure_s` | s | What the body feels is split in two: pressure, the felt force smoothed over this time, and the hit, the impulse beyond it. Only a hit counts toward `thresholds.flinch_impulse_ns`, so a hand that keeps pressing is felt as it lands and not again |
| `reactions.hold_felt` | 0 or 1 | 1: a hold (a hand that holds a part of the body) is felt as a push that is kept up: the body leans into it, steps for it and, while it gets up, is pressed by it. 0: a hold reaches the body as motion only (as it was until 25 November 2026) |
| `reactions.thing_support`, `thing_support_max_up_mps` | 0 or 1, m/s | 1: a simulated thing that pushes up on a part of the body (within 45 degrees of straight up) is what the body stands or lies on, as the floor is: it goes to the sense of balance and is not felt as a hit or a press. Only a thing that has a weight of its own (a hand has none, its gravity is off) and does not come up faster than the second value where it touches (1 m/s; a stick swung up is a hit). However hard it pushes: a fall onto a table is a hard push. 0: every simulated thing that touches the body is a hand to it |
| `reactions.held_push_s` | s | A force felt in every frame for this long is a held push, which the body leans into (`balance.lean_max_cm`) and which moves the capture point a decision is made on. A shove of 0.1 s is not one: its momentum is in the capture point already (with a window of 0.1 s, a 2000 N shove of 0.1 s counted as held, the body leant into it as it fell, and lying it gained 3.2 J) |

## Poses of the body (`poses.json`)

The muscles turn each body toward a target pose. That pose is written here as joint angles and evaluated
by the body's animation instance (`USandboxAnimInstance`, no Animation Blueprint).

| Name | Unit | Meaning |
| --- | --- | --- |
| `poses.blend_s` | s | Time over which the target goes from one pose to the next, eased in and out |
| `poses.<pose>.<joint>.swing1_deg`, `swing2_deg`, `twist_deg` | degrees | How far the joint is turned from the reference pose, about its own axes |
| `poses.fingers.weight` | 0 to 1 | How much of the poses below the fingers take. 0: they stay as modelled. The fingers have no bodies: a pose of them is for the eye and moves nothing |
| `poses.fingers.blend_s` | s | Time in which a hand goes from one pose of the fingers to another |
| `poses.fingers.rest.*`, `fist.*`, `splayed.*`, `flat.*` | degrees | A curl for each finger and the thumb, over its three joints together and counted from the open hand of the reference pose, and how far the fingers part (`spread_deg`). Rest: standing and lying; half of the fist: flinching and staggering; splayed: falling; flat: a hand the world pushes on, as the body gets up |
| `poses.stance.*` | | The standing pose: the reference pose with the arms let down to the sides (upper arms -27 and -5) and the elbows opened by 20 degrees (`lowerarm_*` -20: the reference pose bends them by 37, and a relaxed arm by half that; with 0 the forearms were held forward of the thighs, at -25 the arm is a straight stick). All zeros is the reference pose itself (arms held out), which is what the body held before it had poses |
| `poses.fall_blend_s` | s | Time over which the target goes over to the pose of a fall; placed feet are let go over the same time |
| `poses.fall_back.*`, `poses.fall_forward.*`, `poses.fall_side.*` | | What a falling body does with itself, chosen as the fall starts from the way the pelvis leans and the body moves: backward it sits down and curls up, chin on the chest, hands behind; forward the hands go out in front and the knees bend; sideways (written for a fall to the left, mirrored for the right) the arm of that side reaches for the floor. Kept as the target while the body lies, with the lying state's weak muscles |
| `poses.balance_back.*`, `poses.balance_forward.*`, `poses.balance_side.*` | | What the arms do while the body fights for its balance, added on top of the pose in proportion to how far the capture point is from where the body stands (`balance.arm_*`): going over backward the arms come forward and up, going over forward they go back and out, going over to the left (mirrored for the right) the far arm goes up and out. An arm thrown the way the body tips turns the trunk back the other way |
| `poses.idle.animation` | asset path | The clip whose motion a standing body carries on top of its stance: breathing, the shoulders, a slow sway (the engine's `MM_Idle`, a loop of 7.6 s). Only what each bone does relative to the clip's first frame is taken, for trunk, neck and arms; the clip's pose and its legs are not. Read when the body starts. Empty: no idle |
| `poses.idle.weight` | | How much of the clip's turns the body carries: 1 as recorded (hands travel 4 to 7 cm, the chest 2), 0 none |
| `poses.idle.sway_weight` | | How much of the clip's sway of the pelvis (0.8 cm) the whole body leans by, about its ankles. With both weights 0 a run is bit for bit what it was before the body had an idle |
| `poses.double_over.*` | | Added on top of the pose while the body is doubled over: the trunk 25 and 25 degrees forward, the head 20 down, the upper arms turned in (twist -50) and the forearms bent across the belly (90) |
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
| `effectors.hand.offset_thumb_cm`, `offset_palm_cm`, `offset_knuckles_cm` | cm | In a headset only: where the hand's body is, off the controller's grip pose. Toward the thumb along the handle, the way the palm faces, and toward the knuckles (away from the wrist); the same three numbers serve both hands, mirrored. All 0: the body is at the grip pose, which OpenXR defines as the middle of the closed fist. Set in the headset (M4 part 1, step 4), where it shows whether the spheres are where the hands are. A body that is off the grip pose also gets the velocity the controller's turning gives it |

| `effectors.hand.grip_pitch_deg`, `grip_yaw_deg`, `grip_roll_deg` | degrees | In a headset only: the turn from the controller's grip pose to the hand's own frame (x along the forearm toward the knuckles and the fingers, z toward the thumb, y through the palm). -90, 0, 0 by OpenXR's definition of the grip pose; to be confirmed by the look of the open hand in the headset |
| `effectors.hand.fist_grip`, `fist_grip_slack` | 0 to 1 | The grip above which a hand is a fist and below which it is open, and how far the grip has to pass it to either side before the hand changes (0.5 and 0.1: a fist from 0.6, open from 0.4). Desktop and scripted hands have a grip of 1. The grip of the PS VR2 controllers reads 0 or 1 |
| `effectors.hand.palm_length_cm`, `palm_width_cm`, `palm_thickness_cm`, `palm_forward_cm` | cm | The open hand as a flat plate in the hand's own frame: along the fingers, across the palm, through it, and how far ahead of the body's centre its middle is. Shown while the hand is open. Read when the hand is created |
| `effectors.hand.palm_collides` | 0 or 1 | 1: while the hand is open the plate is the body that collides, in place of the ball: a second body takes the ball's pose and motion when the grip opens and gives them back when it closes. 0: the open hand is only shown, and the ball collides whatever the grip. 1 since the owner's second look in the headset (25 November): he saw a ball and a rectangle and the body answering the ball only |
| `effectors.foot.mass_kg`, `radius_cm`, `stiffness_n_per_m`, `damping_s`, `max_force_n`, `angular_*`, `max_torque_nm`, `snap_distance_cm` | as for the hand | The kicking foot (`foot_left`, `foot_right`): the same body on the same spring as a hand, with a leg's mass and strength, 10 kg, 3500 N and 24000 N/m (`docs/testing.md`, "doubling over, and the kick's strength": a kick into the belly fells the body, into the thigh or the calf it steps; 5 kg and 1800 N, the first values, were no more than a punch). It has none of the hand's other values: a foot has no grip, no open shape and no grab |
| `tools.kick.rest_forward_cm`, `rest_right_cm`, `rest_down_cm` | cm | The desktop's kicking foot: where it rests from the camera, the camera taken as level (20, 12, 100: 28 cm above the floor for a camera at chest height) |
| `tools.kick.speed_mps`, `through_cm`, `reach_cm` | m/s, cm | The desktop's kick (the F key): how fast the foot's target goes to what is aimed at and back (8), how far through that point it goes (15), and how far a kick goes at most from where the foot rests, which is also how far ahead it goes when nothing is aimed at (130) |
| `tools.kick.settle_s` | s | For how long after a kick, and after a jump of the view, the desktop's foot keeps its whole strength (0.4): it comes home at the kick's speed and has to be stopped there |
| `tools.kick.chamber_cm` | cm | A kick is a thrust: the foot comes up to the height of the point aimed at this far before it (50) and goes level from there, the way the camera looks, through the point. 0: straight up to the point, then through |
| `tools.kick.rest_force_n` | N | The force the desktop's foot is held with while it rests (40): it is a body out of sight under the camera, and follows the player without pushing on what it is moved into. It has `effectors.foot.max_force_n` only while it kicks. The strength is part of the foot's target, so a recording has it and a replay's foot is as strong as the session's was |
| `effectors.hand.grab` | 0 or 1 | 1: a grip that closes with a simulated body within reach of the hand takes hold of it (M4 part 3; `docs/testing.md`). 1 since 25 November 2026, on the owner's word after three sessions with it (desktop and headset); `palm_close_on_chest`, which holds what an open hand does when it closes on a body with no grab, switches it off for itself. Desktop and scripted hands whose grip is never opened take hold of nothing |
| `effectors.hand.grab_reach_cm` | cm | How far from the hand's shape a body may be and still be taken (3): a hand that presses on a body is within reach, and so is one that hovers just off it. Measured on 21 pulls: with 0 cm nine take hold of nothing (parts that give way to the hand), with 1 cm three, with 3 and 6 cm none |
| `effectors.hand.grab_slip_cm`, `grab_slip_s` | cm, s | The grip slips when the hand has been further than this from its target for this long (45 cm, 0.25 s). The hand's spring is at its cap from 10 cm on, so a hand left further behind pulls no harder; it is only no longer where the player's hand is. 0 cm: never |
| `effectors.hand.grab_max_force_n`, `grab_overload_s` | N, s | The grip is torn open when its joint has carried more than this for this long (450 N, 0.1 s). The joint carries more than the spring's cap only when something else holds the hand back. 0 N: never |
| `effectors.hand.grab_clear_cm` | cm | A hand that holds something collides with no simulated body. Having let go, it collides again once it lies no deeper than this in one (1): deeper, it would be thrown out; a hand that only rests on what it held is not deeper |
| `effectors.hand.grab_hold_nm_per_rad`, `grab_hold_damping_s`, `grab_hold_max_torque_nm` | N*m/rad, s, N*m | A thing that is held (not a limb of a character, which is held at a point and turns freely) keeps the direction it was taken in: a spring with a torque cap between the hand and the thing (150, 0.05, 40). 0 N*m/rad: the thing only hangs from the hand's point. What turns the hand itself is its own `angular_*` and `max_torque_nm` (20 N*m), and that is what limits a blow with a held stick |
| `effectors.hand.strike_through_cm` | cm | A blow is carried through: while it lasts the fist is asked to be at least this far beyond where it landed, the way it came (40, as much of it as the blow has of its strength), though its target stops at the surface or goes back, as a person's hand does. 0: the fist only follows its target |
| `effectors.hand.strike_return_cm` | cm | After a blow the fist springs back to the hand, which is on its way back. What it meets on that way is no new blow until it is this near its target again (15): without it a fist that had gone 40 cm through a shoulder struck it from behind. 0: a blow may begin at once |
| `effectors.hand.strike_held`, `strike_hold_scale`, `strike_held_full_speed_mps` | 0 or 1, times, m/s | Above 0.5 a thing in the hand strikes as a fist does (1; 0: off): the hand's cap and spring of a blow, graded by the speed of the point of the thing that met a simulated body (none at `strike_speed_mps`, all of it at `strike_held_full_speed_mps`, 9), and for the blow the thing is held and turned up to `strike_hold_scale` (5) times as firmly. The grip does not slip or tear in it. Not for a limb of a character |
| `effectors.hand.strike_force_n` | N | A blow: the force cap of a fist (an open hand has no blow) that has just met something at speed, for `strike_s` (900). At or below `max_force_n`: no blows. Pushing, holding and dragging keep `max_force_n` |
| `effectors.hand.strike_stiffness_n_per_m` | N/m | The spring of a blow (9000). The cap alone does little: the spring asks for its stiffness times the gap to the target |
| `effectors.hand.strike_s` | s | How long a blow lasts (0.15) |
| `effectors.hand.strike_speed_mps` | m/s | A hand slower than this as it meets something has no blow (2) |
| `effectors.hand.strike_full_speed_mps` | m/s | At this speed and above a blow has its whole strength (4.5: the owner's punches in the headset arrived at 2 to 5.6 m/s); between the two it is in proportion |
| `effectors.hand.grab_handle_cm` | cm | A thing that names a handle (a prop's `"handle_cm"`) is taken by it when the hand closes within this distance of the handle (15), and the handle is drawn into the hand; further away the thing is held where the hand is. 0: handles are not looked for. `grab_draw_s` (0.3): for that long after such a grab the handle is still on its way to the hand, and the distance between the two held points is not taken for the joint's stretch |
| `effectors.hand.palm_mass_kg` | kg | Mass of the open hand's plate when it is a body. The fist's mass is `mass_kg`. Both 1.5 until they are tuned on recorded strikes (M4 part 2) |
No gravity acts on a hand: the arm that would carry it is not simulated, and the force cap is not spent on
the hand's own weight. The force of a **contact** can exceed the cap for a moment, because a moving hand
that is stopped gives up its momentum as well; the cap limits what the spring adds.

## Values of the desktop tools (`tools.json`)

| Name | Unit | Meaning |
| --- | --- | --- |
| `tools.hands.rest_forward_cm`, `rest_side_cm`, `rest_down_cm` | cm | Where a resting hand is held, relative to the camera |
| `tools.hands.reach_cm` | cm | How far straight ahead of the camera a reaching hand's target goes. The wheel changes it in a session |
| `tools.hands.reach_speed_mps` | m/s | How fast that target travels out and back (8: a punch) |
| `tools.hands.open_reach_speed_mps` | m/s | How fast an open hand travels (2): a reach that is no blow when it arrives (`effectors.hand.strike_speed_mps`) |
| `tools.hands.through_cm` | cm | A desktop hand sent at something the camera aims at goes this far through it (15), as a fist. An open hand, and a fist with nothing under the crosshair, reach as far as the wheel has set |
| `tools.impulse.impulse_ns` | N·s | What one click of the impulse tool applies. The wheel changes it in a session |
| `tools.impulse.wheel_factor` | factor | One notch of the wheel multiplies or divides the impulse by this |
| `tools.drag.stiffness_n_per_m`, `damping_s`, `max_force_n` | N/m, s, N | The drag handle's spring between the point that was clicked and the point the player moves |
| `tools.wheel_distance_factor` | factor | One notch of the wheel multiplies or divides a distance by this: a hand's reach, the length of the drag handle |
| `tools.camera.speed_mps`, `fast_factor` | m/s, factor | Speed of the free camera, and how much faster it is with the fast key |
| `tools.camera.look_deg_per_count` | degrees | Turn of the camera per count of mouse movement |
| `tools.time.slow_motion_divisor` | count | In slow motion the world steps on one frame in this many. A whole number from 2 |
| `tools.headset.haptic_full_n` | N | In a headset: the contact force of a hand at which its controller vibrates at full strength; below it in proportion. 300 is the spring's cap: a hand held against something vibrates fully |
| `tools.headset.take_buzz_s`, `take_buzz_strength` | s, 0 to 1 | In a headset: how long and how strongly both controllers vibrate when a new take begins |
| `tools.headset.stand_distance_cm` | cm | In a headset (`Tools\play.ps1 -VR`): how far from what the session's camera looks at (the character) the middle of the player's play space is put, on the floor, on the side the desktop camera starts on. 80: arm's length. The player's own place in the play space adds to it |
| `tools.headset.hands_height_cm` | cm | In a headset: how high above the floor the hands wait, at the desktop's rest offsets ahead and to the sides, until their controllers are tracked. Their targets catch up with the controllers from there |
| `tools.headset.max_hand_speed_mps` | m/s | In a headset: a tracked pose that moves faster than this in one frame (20 m/s: 22 cm) is a jump of the tracking, not a motion of the hand, and the target catches up with it instead of jumping. A punch peaks at 8 to 12 m/s |
| `tools.headset.estimated_follow_s` | s | In a headset: for how long a pose that the runtime gives without tracking the controller is still followed (0.15: a hand that leaves the cameras' sight for a moment in a swing goes on). After that the controller counts as lost and its hand stays where it was; in the first session such a pose wandered a metre and a half in five seconds. 0: never followed |
| `tools.headset.runtime_velocity_share` | share | In a headset: how much of the target's linear velocity is the one the runtime tracks (1) and how much the difference of the last two poses (0). On PS VR2 through SteamVR the runtime's velocity trails the motion of its own poses by about 38 ms: replayed on the first recording of a person, the hand is 8.2 cm behind a target faster than 3 m/s with the runtime's velocity and 4.7 cm with the difference (`Tools\swing-lag.ps1`). 0 since 24 November: the owner's choice for the next launch, kept on the data (the hand trembles no more with it: 3.2 cm/s against 3.8 while the controller is still) until the owner has looked |
| `tools.headset.new_take_hold_s`, `new_take_raise_cm` | s, cm | In a headset: both grips held closed for this long, with both hands this far above the eyes, begin a new take: the recording that runs ends, the character is put back, the hands are made anew and a new recording begins (the N key on the desktop). The controllers buzz. Closed grips alone are not asked for: a person who punches holds both closed. A hold of 0: never |
| `tools.headset.catch_up_speed_mps` | m/s | In a headset: how fast a hand's target closes on a controller that was found again after it was lost, was tracked for the first time, or jumped; the controller's own speed is added, so a moving controller is caught too. 1.5: a hand left 116 cm behind is back in 0.8 s, where it flew at 13 m/s when its target jumped |

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
arms (1200 N·m/rad with a cap of 120 N·m, which is a shoulder's published peak: they carry the body while it
pushes itself up), and an assist that pulls the pelvis and the feet toward where the clip has them, each with a
cap on its force (90 N and 30 N an axis since M3 part 7, 250 N and 50 N before: with the clip following the body
over the floor, `getup.pelvis_follow_s`, the stand of 50 falls is as good with these as it was with those, at an
assist share of 0.27 instead of 0.51; at 60 N and 20 N it still passes, with twice as many failed attempts).

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
