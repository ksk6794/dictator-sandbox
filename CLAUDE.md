# DictatorSandbox

Unreal Engine 5.8 C++ prototype: one photoreal room, one active-ragdoll NPC the player can hit, push, grab
and drag, on desktop and PCVR. Requirements, architecture and milestones are in `docs/PRD.md`.
Current milestone: **M1 done** (MetaHuman by script, physics asset and room from JSON specs, baked lighting,
launch in the headset, 7.1 ms of GPU time per stereo frame). **M2, active ragdoll core: all five steps
are built and its six criteria are met; the owner's hand check of the desktop session is deferred and does
not hold up M3** (the deferred list is at the end of the M2 section of `docs/PRD.md`).
The NPC stands under full simulation for 10 minutes (muscles and balance assist through Physics Control);
every tuning value lives in `Config/Tuning`, is hot-reloaded and can be overridden from the console and
the command line; the body has the states Standing, Flinch, Stagger, Falling, Lying and GetUp, joint limits from anatomy
that mirror left to right, and passes the push matrix; the player has physical hands (effectors: bodies
with mass on force-capped springs) that the body feels through its own contact events, a desktop pawn with
test tools, and scenarios can script the hands; debug overlays show the controller's work in a session and
on scenario screenshots; slaps at 6 m/s and punches at 8 m/s on head, chest, thigh and forearm pass without
tunnelling. **M3, balance and recovery, is done: closed on 24 November 2026 with its five criteria met, the fifth by a
review of the clips that the owner handed to the agent; the owner's own look in a session is deferred with M2's
and does not hold up M4** (the seven parts, the gate's table, the review and the known limits are in
the M3 section of `docs/PRD.md`). **M4, full interaction and VR, is in progress: part 1 of 6, the hands in the
headset, is done (four steps)** (its order of parts and what M3 hands over to it are in the M4 section). Step 1:
a session's hands can be recorded (`Tools\play.ps1 -Record`, the K key) and a scenario replays the recording frame
for frame (`"recording"`; `replay_demo_hands`); `Tools\record-replay.ps1` holds a replay against its session
(the same states of the body within a frame, the pelvis within 1 cm, the hands' impulses within 5%) and two
replays against each other by their hashes. Step 2: the hands' source for a headset
(`FSandboxTrackedTargetSource`: the controllers' grip poses with the velocities the runtime tracks, through the
pawn's origin into the world, the body at `effectors.hand.offset_*` from the grip pose, the grip from the squeeze
axis), which asks one interface where a controller is (`ISandboxControllerTracker`; `FSandboxXRControllers` is
the only file that knows of a headset); `Tools\play.ps1 -VR` runs the session in the headset. It is unit-tested
on made-up poses, and a first look in the headset (72 s, `Saved\Recordings\vr_check`) showed poses, velocities and
the squeeze arriving, 89.9 frames a second, and a replay that ends as the session did to the last digit; it also
showed that the runtime's velocity trails its own poses by about 38 ms and that a controller out of sight is not
reported as lost and jumps back by a metre (`docs/testing.md`, "M4 part 1, step 2"). After that look, on the owner's decisions: a controller the headset does
not see leaves its hand where it was and a target never jumps (it catches up at `tools.headset.catch_up_speed_mps`),
and nothing is drawn as text in the headset. Step 3, the hand against its target, is done (`Tools\swing-lag.ps1`):
behind a swing of 2 to 8 m/s the hand is 1.9 to 10.5 cm with the target's true velocity, 2.1 to 13.1 cm with the
difference of two poses, and 4.6 to 26.6 cm with the velocity as the runtime gives it; on the owner's recording
4.7 cm against 8.2 cm, so the velocity is taken from the poses (`tools.headset.runtime_velocity_share` 0). A new
take (the N key; in the headset both fists raised above the eyes with the grips held for a second) ends the recording that runs, puts the NPC back,
makes the hands anew and starts a recording of its own, which replays within the check's limits
(`Tools\record-replay.ps1 -Takes`). Toward part 2, with no physics changed: the hand has a frame of its own
(`effectors.hand.grip_*_deg`), is a fist or open by its grip and shows an open hand as a plate, the controllers
vibrate with the hand's contact force, and the body tells of every hit and change of state. The open hand can
also be the body that collides (`effectors.hand.palm_collides`, on since the owner's second look in the headset on
25 November: an open hand is the plate; a scripted hand's keys say `"grip"`, and without it the hand is a fist). **Part 3's
first step, the grab, is built and on by the owner's decision of 25 November, after three sessions with it**
(`effectors.hand.grab` 1; `palm_close_on_chest` switches it off for itself; every session has it, and on the desktop the G key holds
the hands open): a grip that closes with a simulated body within 3 cm of the hand takes hold of it
by a joint at the hand's own point (`FSandboxGrabJoint`), the force is the hand's spring's, and it lets go when
the grip opens, slips (the hand 45 cm behind its target), is torn open (450 N for 0.1 s) or what it held is
gone; an arm is lifted, a standing body pulled by an arm comes along in steps, a fallen body is dragged 2 m by
two hands and not by one (`Tools\grab-pulls.ps1`: 93 pulls within the stretch and force criteria). One hand
slows a get-up and two hold it, measured on the stand with holding hands (`Tools\getup-stand.ps1 -HoldHands`, three
seeds: from one hand 47 bodies of 146 pull free and stand within the 15 s, from two none; 37 while the open hand was a ball). The body
feels a grab as a push that is kept up (`reactions.hold_felt`, see below); a hand that goes on through the body fells it as the hands are tuned (300 N for half a
second is a knockdown; `effector_push_chest_falls`, `effector_shove_knockdown_gets_up`), while a punch that
comes back delivered 12 to 28 N*s. **Since the owner's looks of 25 November a fist that comes fast has a blow**
(`effectors.hand.strike_*`: for 0.15 s a cap of up to 900 N and a spring of up to 9000 N/m, graded by the speed it
arrived at, from 2 to 4.5 m/s, and carried at least 40 cm beyond where it landed (the owner's decision of 25 November: 25 before), the way it came, though the
player's hand stops at the surface or goes back; an open hand has none, and pushing, holding and dragging keep the
300 N): a fist that comes at 3, 4 and 5.5 m/s is 51, 101 and 120 N*s into the chest, 60, 127 and 154 into the belly and 20, 41 and 47 into the head; one of 5.5 m/s (and the desktop's of 8) fells it,
and the body steps; an open hand at 4 m/s is 7 to 9. In the owner's first session with a blow (`vr_check2`) his
punches were 8 to 34 N*s: his hand was open most of the time and stopped at the surface. The
desktop hand goes at 8 m/s to what the camera aims at and 15 cm through it. **A body hit from the front into the
belly or the solar plexus with 40 N*s or more doubles over for two seconds** (`reactions.double_over_*`,
`poses.double_over`; by its own clock in every state, told as `double_over`): a punch with weight behind it and a
kick do it, a jab, a push and a hit into the chest do not; the push matrix, the knockdowns and the stand are as they
were. **A hit on the head that brings 35 N*s or more knocks an upright body down** (`reactions.knockdown_head_ns`: a kick
does, a punch at 27 at most does not; six cells of the push matrix's 80 N*s row, the blows on the head, now end in a fall).
The kick has its effector (`foot_left`, `foot_right`: a foot of 10 kg on a spring
capped at 3500 N since that day, `effectors.foot.*`; scenarios `strike_kick_*`: into the head a knockdown, into the belly a fall, doubled over, into the thigh and the calf steps; the desktop's kick comes up to the height of its point and is thrust level through it, `tools.kick.chamber_cm`; on the desktop the F key kicks at
what the camera aims at, `tools.kick.*`, and a desktop session's rig and recordings have the foot as a third
effector; at rest the foot is held with 40 N and a kick reaches 130 cm at most). **Part 4's first step:** a scenario
can put boxes with mass into its world (`"props"`; `prop_*`: one pushed, one lifted and carried by a grab, one dropped
on the head, one thrown, one fallen onto, a crate of 40 kg that one hand cannot lift and two can, a stick taken by its end and swung at the arm), and a thing that is held keeps its
direction in the hand (`effectors.hand.grab_hold_*`) and is taken by its handle if it names one (`"handle_cm"`). To the body every such thing is still a hand (standing on a movable plate it reads no foot on the ground), unless
`reactions.thing_support` is on, and it is since the owner's decision of 25 November: a thing that has a weight of its own (a hand has none) and does not come up
faster than 1 m/s where it touches is support, as the floor is, however hard it pushes; the body that falls on it goes limp, as on the floor. A stick's blow is 2.5 N*s, no hit. On the grab: the run and the session tell it what is held (`FScenarioStage::TellBodyOfHolds`) and it
tells its listeners; since the owner's decision of 25 November a hold is felt as a push that is kept up (`reactions.hold_felt`: the body leans into it, steps for it, is pressed by it while getting up), so a pulled thigh or calf is followed by a step in 9 of 24 pulls (2 before). Part 1's step 4,
the owner's two sessions in the headset (25 November, 462 s), is done: the hands and the grab are right, the punches are weak,
a slap does not turn the head (a criterion of N8 A) and the get-up ends like a plank over the toes. **The get-up was
worked on inside M4 that day (three attempts, the toes of N8 D and the ankle's range tried; each below the limit set before its
runs, all taken out) and is put off with its cause written down:** the clip brings its feet under the body while they are loaded on ours, and a loaded
foot does not go (`docs/testing.md`, 25 November). The wrist's model moved to N8 A, and part 3 is closed. Part 4: a thing in the hand strikes as a fist does (`effectors.hand.strike_held`, on with `strike_hold_scale` 5 since the owner's decision of 25 November; 21, 25 and 30 N*s at 3, 5 and 7 m/s into the trunk; `prop_stick_blow_torso`; to be tuned after the owner's look in a session); a room's furniture can have mass (`mass_kg` in a room spec's piece, `movable_room.json`; `room_movable_*`; `Tools\getup-stand.ps1 -FurnitureRoom movable_room`: 50, 49 and 50 of 50); support from things is on (`docs/testing.md`, "M4 part 4: support from things"); the balance goes with what carries the body (`CarrySupport`: a plate of 0.3 m/s is ridden without a step; `prop_ride_plate`, `prop_stand_on_stick`; event `prop_move`). The body senses its own balance (standing feet, support polygon, capture
point); every run measures energy, rest after a fall and penetration of the body
into the world and into itself; the suite runs eight scenarios at a time, and there are tools for baselines
and parameter sweeps. Three defects of earlier milestones were found and fixed in that part: the generated
physics asset had no self-collision, the standing character slept one substep in six, and a run on a busy
machine could differ from the one before it (the solver's determinism mode is now on). Part 2, the source
of the target pose (a native animation instance, poses from text), is done: its step 0 gave the
body collarbones (18 bodies; the shoulder is two links) and the hands three times their inertia, so that a
punch on the forearm does not snap the wrist. Steps 1 to 3 gave the body a native animation instance as the
source of its target pose, poses written as joint angles in `Config/Tuning/poses.json`, and the flinch's
pose offset (the struck part gives way, shoulders up, head in); step 4 two-bone leg placement
(`USandboxBodyController::SetFootTarget`, scenario event `foot_target`). The idle from `MM_Idle` moved to
part 7 then, and was done in the audit after part 4. Part 3, standing balance: the capture point decides whether the body stands, needs a step (held by
the assist until part 4, reported as `step_needed_frames`) or falls; ankle and hip strategies are turns of the
leg targets; the assist's anchors follow the feet; a held push is leant into and makes the body flinch only as
it lands; quiet-standing assist share 0.0052 (bound 0.0093); the push matrix names each outcome (stood, held,
stepped, fell) and judges it by the owner's table (the 80 N*s row from part 4). Part 4, Stagger and the recovery step: a body whose
capture point leaves what the ankles and hips bring back staggers and steps (to where the capture point will be
when the foot lands, across the other foot if it must, again if one step is not enough), and can be asked for a
step (`RequestStep`, scenario event `step`). An audit of how the body moves (November 2026, on the owner's
request) followed it: the stance has the arms down, arms and neck are soft, the body always tries to step and
falls only when its posture gives way, a falling body takes a protective pose and a lying one lets go
(`poses.fall_*`), after recovery steps the body brings its feet back together (`balance.regroup_*`), a standing
body carries the motion of the engine's `MM_Idle` on top of its stance (breathing, the shoulders, a slow sway;
`poses.idle.*`, the idle planned for part 7), and its arms join the fight for balance (`poses.balance_*`,
`balance.arm_*`). **Part 5, the fall, is done but for one look that waits for the owner:** knockdowns from 8
directions pass (`Tools\knockdowns.ps1`), the muscles have torque caps from published strength figures, they
let go after the body meets the floor, joint limits are written in the spec, and continuous collision detection
stays off by measurement; whether the poses of Epic's Game Animation Sample are better than these is looked at
once the owner has downloaded it (done: its get-up clips are taken, its logic is not). **Part 6, the get-up, is
done (three steps).** A recorded clip is a source of the target pose (`FSandboxClipPose`: the four
get-ups of Epic's Game Animation Sample, made for another skeleton, carried over to the body's 17 joints;
`USandboxBodyController::SetClipPose`, scenario event `clip_pose`, `Tools\clip-film.ps1`). **Step 2 is done:** the
state GetUp. A body that has lain still for `getup.rest_s` takes the clip whose pelvis lies most like its own and
gets up by it under full simulation: the muscles are asked for the clip's pose, the clip's time waits for the body,
the assist pulls the pelvis and the feet toward where the clip has them with capped forces, and the clip is laid
over the feet the body really stands on. It gets up from all four sides in 3.3 to 4.1 s; a 30 N*s push does not
stop it, a 200 N*s blow fells it and it tries again, two hands at their 300 N each only slow it, and a force it
cannot lift (1500 N on the pelvis) is waited out for 3 s, given up and tried again (scenarios `getup_*`). A scenario's body gets up only with `"get_up": true`. **Step 3, the stand:** `Tools\getup-stand.ps1`
knocks the body down 50 times from a seed, 24 of them near a wall, a table and a chair (static boxes of a room
spec, `Specs/rooms/getup_room.json`: the table in the middle of the room, the chair drawn up to it, since the
owner's request of 24 November), and counts the falls that end standing: 49, 50 and 50 of 50 over three seeds
(the criterion is 90%), 39, 37 and 43 of them at the first attempt (50, 49 and 48 with the two pieces apart near
the corners). **Part 7, the gate, is done (four steps and the review).** The get-up's assist is
0.28 of body weight on average, from 0.51, with the stand unchanged (49, 50 and 49 of 50): the clip follows the
body over the floor while the feet carry nothing (`getup.pelvis_follow_s`), the arms are stronger while getting
up, and the assist's caps are 90 N on the pelvis and 30 N on a foot. A body getting up that is pressed with
450 N or more does not push on (`getup.hold_force_n`): 600 N keeps every body of 150 down, under 300 N one of
three gets up while it is pressed (`Tools\getup-stand.ps1 -PressN`; two of three before the body's friction on the
floor went from 0.7 to 0.5, so "one hand slows" is set again in M4 with the grab). The stance's elbows hang at 17 degrees of bend, not 37
(`poses.stance.lowerarm_*`). The wrist's model waits for M4's grab by the hand (owner's decision). **Step 2,
friction and restitution, is done:** the body's surface is tuning (`surface.*`: on the floor a friction of 0.5 for
skin and cloth and 0.7 for the soles, where both were the engine's 0.7; restitution 0.1, taken as the smaller of
the two surfaces'), measured by dragging the lying body (`Tools\surface-friction.ps1`: 0.69 before, 0.51 to 0.54
now). A body with 0.9 against the floor gets up at the first attempt 101 times of 150 instead of 124 to 130;
otherwise standing, pushes, falls and get-ups hardly notice friction from 0.4 to 1.0 or restitution from 0 to
0.3. A lying body needs about 400 N to be dragged: one hand does not move it, two do. **Step 3, the sweeps, is
done and changed no value:** 64 solver iterations is the only count that passes everything (16 and 8 break the
bound on the standing assist's share, 32 to 48 lose two cells of the push matrix; 32 would save 0.15 ms a frame),
the legs' strength is held from both sides (at 400 the body does not step, at 1200 a blow of 200 N*s on the chest
does not fell it), the other muscles and the dampings are on a plateau, and the body stands with no assist at all
but then takes up to seven steps after 30 N*s on the head. **Before step 4 (owner's decisions of 24 November), both done:** the push matrix counts a
step from 10 cm (`-StepMinCm`; a foot that hops less is a shuffle, counted and not judged: the cells lost at 32
iterations and with a weaker assist were such hops of 5 to 6 cm), and the stance on crossed legs that Stagger did
not leave is closed: a staggering body that has come to rest with its centre of mass off the support that touches
steps once more (`balance.rest_step_s`; scenario `stagger_rest_off_support`), where it had neither stepped (the
capture point within 4 cm of its whole soles) nor stood (the centre of mass not over the edges it stood on), kept
up by the stagger's upright hold on the pelvis. **Step 4, the clips and the criteria table, is done, and so is the review**
(`Saved\Films\m3_*`, listed in `Saved\Films\m3_gate.txt`; the table is in the M3 section of `docs/PRD.md`). The
review left the runs after 200 N*s and the step across as they are, and changed one thing: a get-up attempt is
given up once its clip has waited 5 s in all with nothing pressing on the body (`getup.wait_s`; a body had held
one pose for 13 s under a table's edge; scenario `getup_wait_bounded`). The stand is 49, 50 and 49 of 50 with 38,
44 and 44 at the first attempt, and no attempt lasts longer than 7.7 s.
DLSS is deliberately deferred to M5.

Everything is driven from the command line. Do not rely on the Unreal Editor GUI.

## Commands

Run from the repo root in PowerShell. Each script prints a one-line verdict first (`BUILD OK`,
`SCENARIO FAIL: ...`), returns a meaningful exit code and writes a JSON result under `Saved\`.

| Task | Command |
| --- | --- |
| Clean clone to verified build | `powershell -ExecutionPolicy Bypass -File Tools\bootstrap.ps1` |
| Compile | `powershell -ExecutionPolicy Bypass -File Tools\build.ps1` |
| Unit tests | `powershell -ExecutionPolicy Bypass -File Tools\test.ps1` |
| One scenario, headless | `powershell -ExecutionPolicy Bypass -File Tools\scenario.ps1 -Name drop_passive` |
| One scenario with a screenshot | add `-Shot` (renders offscreen, saves `final.png` next to the metrics); `-ShotAt "3.07,3.25"` adds pictures at those simulated times (`shot_3.070.png`) |
| Debug overlays in the pictures | add `-Overlay all` to a `-Shot` run or to `play.ps1` (or layers: `com,support,capture,joints,assist,contacts,state,hands`); in a session the V key, in any console `Sandbox.Overlay all\|off\|toggle\|<layers>` |
| One scenario in the headset | add `-VR` (headset on, SteamVR running; takes the scenario's duration in real time) |
| One scenario with other body tuning | add `-Tune "muscles.legs.stiffness_nm_per_rad=400,solver.position_iterations=32"`, or `-TuningDir <dir>` for another set of tuning files |
| All scenarios | `powershell -ExecutionPolicy Bypass -File Tools\suite.ps1` (eight engine processes take the scenarios from one queue; also holds the whole set to 2 cm of joint stretch and a 1.5 ms mean physics step, and prints the worst scenario of each; `-Long` adds the scenarios over 60 s, `stand_npc_10min`; `-Parallel 1` for physics step times nothing disturbed; `-Tune "name=value,..." -Label <name>` runs the whole suite against other values into `Saved\Suites\<name>`, leaving what baselines are made of alone) |
| A motion as a sheet of pictures over time | `powershell -ExecutionPolicy Bypass -File Tools\film.ps1 -Name hit_chest_200_falls -Times "3.1,3.2,3.3,3.4,3.5,3.7,4.0,5.0"` (`-Camera side\|front\|back\|quarter`, `-Events <file>`, `-Tune`; `Saved\Films\<name>\sheet.png`; `-Around 3.0 -Span 0.6 -Every 0.033` for a dense strip instead of `-Times`, `-Video` for an mp4 as well) |
| What a recorded clip asks of the body, as a sheet of pictures | `powershell -ExecutionPolicy Bypass -File Tools\clip-film.ps1 -Clip M_ragdoll_getup_stand_B -ClipTimes "0,0.4,0.8,1.2,1.6,2.0,2.5" -Carry -RaiseCm 60` (the body hangs by its welded pelvis and is asked for the clip's pose at each time; `-Carry` moves the pelvis as the clip does; several clips separated by commas; `Saved\Films\clip_<name>\sheet.png`) |
| Keep the suite's results, or see what changed since | `powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -Save <name>` before a change, `-Compare <name>` after it (or `suite.ps1 -SaveBaseline <name>` and `-Baseline <name>`): which scenarios have other hashes, and what moved in them |
| Scenarios over a set of tuning values | `powershell -ExecutionPolicy Bypass -File Tools\sweep.ps1 -Name legs -Scenarios "stand_npc,hit_head_30_stands" -Vary "muscles.legs.stiffness_nm_per_rad=400,800;muscles.legs.damping_s=0.03,0.06"` (every combination, or `-OneAtATime`; `-Metrics` chooses the columns; table in `Saved\Sweeps\<name>`) |
| Push matrix (160 pushes, outcome by the owner's M3 table) | `powershell -ExecutionPolicy Bypass -File Tools\push-matrix.ps1` (`-PushSeconds 0.1` for pushes instead of blows, `-Tune` to try values, `-Impulses "30" -Enforce "30"` for one row, `-Rules m2` for the old gate; a step is a foot carried 10 cm or more, `-StepMinCm`, and shorter hops are reported as shuffles); against a baseline: `Tools\baseline.ps1 -CompareMatrix <name>` |
| The stand of 50 falls and get-ups (the criterion of M3 part 6: 90% end standing) | `powershell -ExecutionPolicy Bypass -File Tools\getup-stand.ps1` (26 falls on the bare floor, 8 each near a wall, the table and the chair; `-Seed 2` for other falls, `-Only "table,chair"`, `-Tune`, `-Film failed` for a sheet of pictures of every fall that does not end standing, `-Film "36,42"` of those trials; `-PressN 300` presses every fallen body down for 20 s and counts those that stood under it; `-PropsFile Tools\stand_furniture_props.json -Only floor -Floor 50` puts movable furniture around every fall on the bare floor; `-HoldHands 1` or `2` has that many hands take hold of the chest of every fallen body for 15 s, with the grab on, and counts the grips that came off and the bodies that pulled free and stood in that time; a trial ends once its body has stood for 3 s, `-StopStandingS 0` runs each for the whole 40 s; the places are worked out from where the room spec has the table and the chair) |
| The stand on three seeds side by side, one line a seed | `powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label <name>` (`-Tune`, `-Seeds "1,2,3"`, `-PressN`, `-Only`; three engine processes a stand, nine in all; results in `Saved\Matrix\getup_seeds\<name>`) |
| Friction between the lying body and the floor, by experiment | `powershell -ExecutionPolicy Bypass -File Tools\surface-friction.ps1` (knocks the body down and drags it by the pelvis with `-ForceN 800` for `-PushS 1.0`; the friction from its acceleration; `-Tune` to try values) |
| Knockdowns from 8 directions, judged by the fall's criteria | `powershell -ExecutionPolicy Bypass -File Tools\knockdowns.ps1` (a blow of 200 N*s on the chest from each; `-Impulses "200,300" -Bodies "spine_04,neck_01"` for harder ones and the head, `-Tune`, `-CVars`, `-Film` for a sheet of pictures of every fall; `-ByHand` fells it with two fists that go through the chest at 4 m/s instead) |
| One generated scenario file | `powershell -ExecutionPolicy Bypass -File Tools\scenario.ps1 -File <path.json>` |
| Interactive session (a window, for a person) | `powershell -ExecutionPolicy Bypass -File Tools\play.ps1` (`-Name <scenario>` opens that scenario's scene; keys are on the screen; the grab is on; `-Grab` switches the grab on for the session: G holds the hands open, and a hand that closes on the body holds it) |
| Self-test of the session, no window | `powershell -ExecutionPolicy Bypass -File Tools\play.ps1 -Smoke` (`-Smoke -Shot` renders it and saves `final.png`) |
| The grab in a session without a person | `powershell -ExecutionPolicy Bypass -File Tools\play.ps1 -Demo -Grab` (the grab switched on: the right hand opens, reaches into the chest and takes hold; the character is put back while it is held; the hand takes hold of the new one, pulls and lets go; a step of the bootstrap) |
| Record the hands of a session | `powershell -ExecutionPolicy Bypass -File Tools\play.ps1 -Record <name>` (from the first frame; the K key ends a recording and starts the next; `Saved\Recordings\<name>\`: the recording, a scenario that replays it, what the session saw) |
| The session in the headset | `powershell -ExecutionPolicy Bypass -File Tools\play.ps1 -VR` (headset on, SteamVR running, controllers awake; the hands follow the controllers, the player stands 80 cm in front of the NPC, Escape in the window ends it; with `-Record <name>` as on the desktop; `-Seconds 180` ends it by itself and keeps the recording; afterwards it prints what was tracked, which controller keys arrived and how many frames a second went by; without a headset it ends after about 40 s with `PLAY ERROR`) |
| A session's hands recorded and replayed, held against each other | `powershell -ExecutionPolicy Bypass -File Tools\record-replay.ps1` (a session without a person uses the hands alone, `play.ps1 -Demo`; `-Recorded <name>` for a recording a person made; `-Twice` also wants two replays with the same hashes; `-Takes` runs a session of three takes, `play.ps1 -Demo -Takes`, and holds each take's replay against it; `-PelvisCm`, `-ImpulsePct`, `-Frames` are the limits) |
| What is in recordings of a person's hands | `powershell -ExecutionPolicy Bypass -File Tools\recording-report.ps1 -Name "takes*"` (every recording under `Saved\Recordings` that matches is replayed and read: how fast each hand's target went, how often its grip closed, what each hand delivered and to which bones, every hit the body told of with the hand's speed as it arrived and the largest contact force, what the body did, and whether the replay ends as the session did; `-Tune` replays with other values; `Saved\Reports\<name>\report.json`) |
| Grabs on seven bodies, pulled five ways at three speeds, judged by the grab's criteria | `powershell -ExecutionPolicy Bypass -File Tools\grab-pulls.ps1` (`-Bodies "lowerarm_l,neck_01"`, `-Speeds "1,2,4"`, `-Directions "left,front,up"`, `-DistanceCm`, `-HoldS`, `-Tune`; where each body's surface is, it measures first with a hand of 20 N; a row a pull: what the body did, what the grip did, the joint's force, the stretch of the grab and of the body's joints; `Saved\Matrix\grab_pulls`) |
| The scorecard of the natural body: a row a criterion of `docs/natural-body.md` | `powershell -ExecutionPolicy Bypass -File Tools\natural.ps1 -Label <name>` (`-Baseline before_track` prints that report's figures beside this one's, `-Quick` leaves out the stand of 150 falls, `-Tune`, `-NoPrior`; reports and judges nothing; `Saved\Natural\<label>\report.json`; C25 is read from the latest suite) |
| How far a steady push moves the standing body | `powershell -ExecutionPolicy Bypass -File Tools\compliance.ps1` (20 and 60 N for 1.5 s on eight bodies from the front, the back and a side; `-Bodies`, `-Forces`, `-Ways`, `-Tune`, `-Idle`; `Saved\Matrix\compliance`) |
| The database of recorded poses for `metrics.pose_prior` | `powershell -ExecutionPolicy Bypass -File Tools\pose-prior.ps1 -Build` (from the clips `Specs/pose_prior.json` lists, copied from Epic's sample by `fetch-sample-anims.ps1 -PosePrior`; not committed; without `-Build` says whether it is there and current) |
| How far a hand lags behind its target | `powershell -ExecutionPolicy Bypass -File Tools\swing-lag.ps1` (scripted swings that peak at `-Speeds "2,4,6,8"` over `-DistanceCm 60`, each with the target's velocity given exactly, as the difference of two poses, as the headset's runtime gives it and not at all; `-Recording <file> -FromS 26 -ToS 61` adds a person's recording replayed three ways; `-Tune` for other hands; 35 s with a recording) |
| Run-to-run deviation | `powershell -ExecutionPolicy Bypass -File Tools\repeatability.ps1 -Name drop_passive -IncludeRendered` |
| Compare two runs | `powershell -ExecutionPolicy Bypass -File Tools\compare-runs.ps1 -A <dir> -B <dir>` |
| Regenerate assets from specs | `powershell -ExecutionPolicy Bypass -File Tools\gen-assets.ps1` (`-Force` also rebuilds up-to-date maps) |
| Bake a map's lighting | `powershell -ExecutionPolicy Bypass -File Tools\bake-lighting.ps1 [-Quality Preview] [-Force]` |
| Create, rig and assemble the MetaHuman | `powershell -ExecutionPolicy Bypass -File Tools\metahuman.ps1` (cloud steps need an Epic sign-in; see below) |

Typical times on the reference machine: incremental build 4 to 35 s, full build 80 s, one scenario 8 s
(the 10-minute stand scenario 60 s), unit tests 15 s, the whole suite 43 s (117 scenarios; about 90 s with `-Long`), the scorecard 140 s (`-Quick` 55 s), the compliance table 25 s, the pose database 35 s, a record and replay 25 s (three takes 40 s), a report on a 72 s recording 40 s, the hand's lag on four speeds 35 s,
the push matrix 50 s (one row of 40: 15 s), the knockdowns 18 s, the grab's 93 pulls 60 s, the stand of 50 falls 66 s (three seeds side by
side 110 s), regenerating the physics asset 10 s, a film
of 12 pictures 15 s, the session's self-test 10 s (its grab demo 12 s), the whole
bootstrap about 5 minutes (14 steps). Engine start-up is 14 s of a run against 1 s of physics, which is why runs share
processes; `SANDBOX_SEPARATE_PROCESSES=1` goes back to one process per run.

The loop: edit, `build.ps1`, `test.ps1`, `suite.ps1`, read the JSON, repeat. A task is done when its
scenarios pass, not when the code compiles. For anything visual, run a scenario with `-Shot` and look at the PNG.

Before a change to the body or its controller, take a baseline (`suite.ps1 -SaveBaseline <name>`); after
it, `suite.ps1 -Baseline <name>` names every scenario whose hashes moved. Scenarios the change should not
touch must not be in that list, and the ones that are get explained in the part's report.

## Layout

| Path | Holds |
| --- | --- |
| `Source/SandboxCore` | Shared services: log category `LogSandbox`, JSON helpers, `FSandboxParamStore` (tuning files, overrides, hot reload, the `Sandbox.Tune*` console commands). Depends on no other Sandbox module. |
| `Source/SandboxBody` | The simulated body: `ASandboxRagdollActor`, `USandboxBodyController` (muscles, balance assist and the states Standing, Flinch, Falling, Lying; it tells whoever listens of every hit and change of state, `OnBodyEvent`, which a run lists under `control.events`, and of a grab and its release, which it does not feel and is told of by the run or the session, `TellHolds`; the sense of balance, `GetBalance` and `MeasureBalance`: standing feet, support polygon, capture point; the decision on it, `IsStepNeeded`; `FollowSupport` moves the assist's anchors with the feet; `UpdateStrategies` turns the leg targets (ankle, hip, lean into a held push); `StartGetUp` and `UpdateGetUp` get a lying body up by a recorded clip (the state GetUp: the clip chosen by how the pelvis lies, its time waiting for the body, the pelvis's and the feet's assist aimed where the clip has them, the clip's root following the loaded feet); `ApplySurface` gives the bodies their friction and restitution (`surface.*`); `FeelFrame` splits what is felt into hit and pressure; `GetControlView` hands its targets to overlays), `FSandboxBodyTuning` (the body's tuning values by name, read from the store, poses among them), `FSandboxBodyProbe` (measurements: the support polygon, the capture point, energy, penetration into the world and into itself), `USandboxAnimInstance` (the target pose the muscles hold: a base pose blended from one to the next, turns added on top such as the flinch's; a native instance with its own proxy, no Animation Blueprint), `FSandboxPoseRig` (joint angles of a pose to bone rotations, on the axes of the physics asset's joints), `FSandboxIdleMotion` (the motion of a recorded idle clip, as each bone's turn from the clip's first frame, added to the pose of a standing body), `FSandboxClipPose` (a recorded clip made for another skeleton as target poses for the body's joints: each body turned, in the space of the whole body, by what the clip's bone is turned from its own reference pose, arms and legs pointed where the clip's point, every joint kept within its range; laid over the base pose by the animation instance's layer, `USandboxBodyController::SetClipPose`), `FSandboxMotionStats` (arithmetic on recorded motion: spread, period, a drive's torque), `FSandboxPosePrior` (a database of recorded poses and the likeness of a pose to them). |
| `Source/SandboxInteraction` | The player side. `USandboxEffectorComponent` (a hand, or a kicking foot, which is the same with other tuning and no grip: a sphere with mass, a fist or open by its target's grip; the open hand is shown as a plate and, with `effectors.hand.palm_collides`, is a second body, `USandboxPalmComponent`, that takes the ball's place while the hand is open; ask the hand through `GetHandTransform`, `GetHandVelocity`, `GetProbe`, not the component) held by `FSandboxWorldSpring` (spring with a force cap to a moving world point); `ASandboxEffectorRig` (the hands plus an `ISandboxTargetSource`: desktop, scripted, recorded, tracked; it can keep the targets of every stepped frame, `SetRecording`); `FSandboxTargetRecording` (what the hands were asked for, frame after frame, as a text file) and `FSandboxRecordedTargetSource` (plays one back frame for frame); `FSandboxTrackedTargetSource` (the hands of a player in a headset, as arithmetic on what an `ISandboxControllerTracker` says of each controller) and `FSandboxXRControllers` (that interface answered by the engine's XR system: the only file that knows of a headset); `FSandboxGrabJoint` (a grab: a joint that keeps a hand's own point and another simulated body together, free to turn; the hand takes hold with it when its grip closes within reach of a body, `effectors.hand.grab`, and passes simulated bodies while it holds); `FSandboxDragHandle`; `ASandboxPawn` (free camera, tools, time control, the kick and the open hands of the desktop, input made in C++; in a headset the origin of the play space, with the camera under it as the head), `ASandboxPlayerController`, `ASandboxHUD`, `ASandboxGameMode`; `FSandboxInteractionTuning`. |
| `Source/SandboxScenario` | `UScenarioBatchSubsystem` (the job queue: one engine process runs many scenarios, each in a fresh world, with its own `run.log`, after a warm-up run; `-ScenarioQueue=<dir>`), `UScenarioRunnerSubsystem` (also measures the hands from outside: penetration, tunnelling, what a strike moved, and every frame each hand's gap to its target, written to `hands.csv`; and for the body, every frame: balance, energy, rest after a fall, penetration, written to `series.csv`), `FScenarioSpec`, `FScenarioStage` (builds a scenario's scene, its things with mass among it: `"props"`, boxes that simulate, `SpawnProps`), `USandboxPlaySubsystem` (the interactive session, `-Play=`; the only place where pawn and character meet in code), `USandboxOverlaySubsystem` (debug overlays, `Sandbox.Overlay`; here because it needs both the character and the hands), `FNaturalMeasures` (how natural the motion is, frame by frame, read only; the runner calls it), automation tests. |
| `Source/SandboxEditor` | Editor-only `GenerateAssets` commandlet; `FPhysicsAssetGenerator` builds physics assets from specs, joints from their anatomy (`ComputeJointFrames`). |
| `Specs/` | Text specs for generated assets (physics assets, room maps with their furniture); format in `Specs/README.md`. |
| `Scenarios/` | Scenario JSON files; format in `Scenarios/README.md`. |
| `Recordings/` | Recordings of a session's hands that scenarios replay (`"recording"` in a scenario file); new ones are written under `Saved\Recordings` and copied here to be kept. |
| `Tools/` | The CLI scripts above. `common.ps1` holds shared helpers (`Invoke-ScenarioBatch` hands scenario runs to a queue that several engine processes empty, for the suite, the push matrix, sweeps and single runs; a run's own log is `run.log`, the process's `process.log`). |
| `Config/` | Engine and game settings. Physics timing lives in `DefaultEngine.ini`. |
| `Config/Tuning/` | Every tunable number, as JSON: `body.json` (the character), `effectors.json` (the hands), `tools.json` (the desktop tools); names, units and ways to change them in its `README.md`. |
| `docs/` | `PRD.md` (plan), `natural-body.md` (the natural-body track, proposed on 25 November 2026: what "natural" means in 26 criteria with human reference figures, and parts N1 to N9 with their design, scenarios and steps; **N1, the measures and the scorecard, is done**, and the reconnaissance before N8 and N2; no other part is started), `testing.md` (repeatability policy and measurements). |

## Rules

- **Module dependencies.** `SandboxBody` and `SandboxInteraction` must not depend on each other; they meet
  only as rigid bodies in the physics world. Both may depend on `SandboxCore`. `SandboxScenario` may depend
  on all of them; nothing depends on it.
- **No Blueprint logic, no Animation Blueprints.** Gameplay, input and animation logic are C++.
- **Text is the source of truth.** Never hand-edit `.uasset` or `.umap` files. Generated assets come from
  `Specs/` through the `GenerateAssets` commandlet; imported art is imported by script.
- **Tuning values live in `Config/Tuning`, not in code.** A new tunable number gets a name in its module's
  list (`VisitValues` in `SandboxBodyTuning.cpp` for the body, in `SandboxInteractionTuning.cpp` for hands
  and tools) and a value in the JSON file; no default in C++. A value missing from the files, or a name the
  code does not know, is an error by design.
- **The pawn does not know the character and the character does not know the pawn.** The character feels a
  hand through its own contact events (`USandboxBodyController::OnBodyHit`); the drag handle pulls whatever
  body was clicked. What needs the character itself (the calibrated impulse, the reset) the pawn only
  requests through a delegate, and `USandboxPlaySubsystem` in `SandboxScenario` carries it out.
- **A scenario run has no pawn.** The runner replaces it with a bodiless spectator, so a scenario's world
  holds only what its file puts there. Hands in a scenario come from `effector` events.
- **Overlays only look.** `USandboxOverlaySubsystem` reads bodies through const accessors and listens to
  their contact events; it writes nothing to a body and changes no setting of one. Without a renderer it
  stays off, does not tick and listens to nothing. A run must give the same hashes with overlays and without.
- **Measuring changes nothing.** What the runner and the body's balance sense read (bodies, scene queries,
  contact events) must leave a run's hashes as they were. A new metric is checked that way before it is used.
- **The body can be interacted with at every moment** (owner's requirement, FR-18). In every state, getting up
  included, it is a simulated body whose muscles are asked for a pose: never kinematic, never played back, never
  deaf to a contact. A recorded clip is a target for the muscles, not a motion of the body, and every state has
  to say what a hit, a push and a grab do to it.
- **A body under control never sleeps** (`USandboxBodyController::Start` sets its bodies to never sleep). It
  has to sense its balance and answer a touch at once; see the pitfall about the solver's sleep below.
- **Time control never changes the physics step.** Pause, single step and slow motion decide on which
  frames the world steps; a step is always 1/90 s in 2 substeps. No time dilation.
- **Physics timing is fixed.** Synchronous physics, 90 Hz frame, 2 substeps (180 Hz). Scenarios force a
  fixed engine timestep. Do not read wall-clock time in simulation code. The solver's determinism mode is
  on (`p.Chaos.Solver.Deterministic=1` in `Config/DefaultEngine.ini`); every scenario run checks it. Every
  scene a scenario or a session builds also keeps the solver's background rebuild in step with the simulation
  (`FScenarioStage::KeepSolverInStep`), without which a fall depends on how busy the machine is.
- **Units.** Unreal units in code (cm, kg, cm/s). Scenario files and metrics use the unit in the field
  name (`_cm`, `_s`, `_ns` for newton-seconds, `_mps`).
- **Tests are named `DictatorSandbox.*`.** The filter is a substring match, and engine plugins already use
  the word "Sandbox".
- **Result files over exit codes.** Scripts decide success from the JSON a run writes; an editor process
  can exit 0 after an error.
- **Close the Unreal Editor before building.** It locks the module DLLs. Live Coding is not used.
- Use `UE_LOG(LogSandbox, ...)` for project logging.

## Things that have already bitten

- **`/Engine/Tutorial/.../TutorialTPP` is not a usable ragdoll.** It explodes when all bodies simulate.
  Use the stock mannequin copied by `Tools\fetch-stock-content.ps1`
  (`/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple`).
- **Physics assets can contain non-rigid constraints.** The stock mannequin has one (`UserConstraint_0`).
  `FSandboxBodyProbe::MaxJointStretchCm` only measures joints with all linear axes locked.
- **Headless runs do not render, so nothing is "recently rendered".** Skeletal meshes must use
  `AlwaysTickPoseAndRefreshBones` and disable update-rate optimisation, as `ASandboxRagdollActor` does.
- **Keep the `AndroidFileServer` plugin disabled in the `.uproject`.** When it is enabled, the editor
  appends a section with a generated `SecurityToken` to `Config/DefaultEngine.ini` on first run, which
  must not be committed.
- **Toolchain.** Engine 5.8.4 from the Epic launcher; Visual Studio 2026 with MSVC 14.50 and Windows SDK
  10.0.22621. The MSVC 14.51 toolset breaks 5.8. `Tools\common.ps1` finds the engine from the launcher's
  install list, or from `UE_ROOT` if set.
- **Do not make one ragdoll body kinematic to hold it in place.** Kinematic bones follow the component, and
  the component follows the simulated root, so the body is dragged along. Constrain it to the world instead
  (see the `pin` event in `UScenarioRunnerSubsystem`).
- **The stock MetaHuman physics asset cannot be simulated as it is.** Three defects, all fixed by the
  generator for `PA_SandboxNPC` and all still present in `PHYS_SandboxNPC` (scenario `drop_metahuman`):
  joint anchors sit 1.5 to 3.6 cm apart on all 15 joints, because they were placed for another body's
  proportions (M1 reported only the worst one, `spine_02`); every joint has an orientation motor that pulls
  it away from the reference pose; and the foot shapes do not lie flat on the floor. See `Specs/README.md`.
  A body that jumps or changes shape in its first frames has one of these; `metrics.json` shows the anchor
  gap and the spawn-pose angles per joint.
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
- **Chaos puts a standing character's island to sleep, and the controller's next tick wakes it.** A body
  that holds still is still enough for the solver's sleep thresholds. Under control it then slept one
  substep in every six: no gravity, no contacts and no contact events in that substep. Nothing looked
  wrong; the feet reported five sixths of the body's weight, the balance assist's load read 0.047 of body
  weight instead of 0.0185, and feet at 8 solver iterations rocked twice as fast as they do awake. Found
  only because the floor's push on the feet did not add up to the weight. The controller's bodies are now
  `ESleepType::NeverSleep`. When a sum of forces or impulses is short by a simple fraction, suspect sleep
  (`-CVars "p.Chaos.Solver.Sleep.Enabled=0"` to compare).
- **`FPhysicsAssetUtils::CreateNewBody` switches collision off between the new body and all earlier ones**
  unless `FPhysAssetCreateParams::bDisableCollisionsByDefault` is cleared. The generated NPC asset had every
  one of its 120 pairs off from M1 to M3: no self-collision, a forearm 17 cm inside the chest after a fall.
  The generator's log listed "colliding bodies" from its own plan, not from the asset. Read such a fact
  back from the asset: `penetration.self_collision_pairs` in `metrics.json` is what the run-time asset
  allows (127 for the NPC), and a metric that reads exactly 0 in every run is a metric to distrust.
- **A strike scenario can pass because it lands on a lucky spot.** `strike_punch_forearm` passed M2 at
  10 rad/s; 2 cm higher the same punch snapped the wrist at 37 rad/s, and adding the collarbones moved the
  arm enough to land in that band. Sweeping the point of impact (and the stiffness: 11, 49, 41, 35, 30,
  38 rad/s for 100 to 300 N*m/rad) showed it. A figure that jumps back and forth as a setting changes
  smoothly is a scenario on an edge; strike more than one point (`strike_punch_forearm_heights`).
- **A muscle target that jumps puts energy into the body.** The flinch's pose offset first ended with the
  Flinch state; a hit that knocked the body over ended it in one step, the muscles snapped the falling body
  back, and the run gained 13.8 J (`max_energy_gain_j` caught it). Anything added to the target pose fades by
  its own clock, not with a state.
  The same holds for a foot placement switched on at full weight (the calf snapped at 84 rad/s): weights
  ease in over `poses.blend_s`.
- **A point fixed in the world is a moving target for a body that turns.** A leg placed at a world point
  on a body pinned by a ball joint tipped the body, the point moved against it, the leg chased it, and the
  body fell. Test a placement on a welded pelvis (`"lock_rotation": true` on a `pin` event).
- **A pose of zeros must leave a run bit for bit as it was.** `FSandboxPoseRig::Build` keeps the reference
  rotation itself for a joint at 0, and the animation instance writes only bones some pose has turned:
  `Ref * Frame * Identity * Frame^-1` need not round to `Ref` exactly (not measured; the code does not
  rely on it), and a hash moves with the last bit.
- **Chaos's inertia conditioning is global and slows a fallen body's rest.** Raising
  `p.Chaos.Solver.InertiaConditioning.Distance` (default 20) calmed the wrist and the standing feet, but a
  lying body then took 3.3 to 6.7 s to come to rest instead of 2.6. The project scales one body's inertia in
  its spec instead (`inertia_scale`, the hands).
- **Bodies that collide must not overlap in the reference pose.** They push each other apart from the first
  step: with the two thighs colliding (1.4 cm into each other) the standing character's feet turned at
  1.3 rad/s and the assist's load went from 0.05 to 0.22. Such pairs go under `collision.disable` in the
  spec; `penetration.spawn_self_cm` and the check `spawn_pose_clear_of_itself` find them.
- **Without its determinism mode, Chaos can give a run that differs from the one before it.** It gathers
  contact constraints from worker threads in the order they finish. On a busy machine one run of
  `drop_passive` in about 25 came out differently from the tenth digit on; 96 runs made on purpose did not
  show it again. The mode (`p.Chaos.Solver.Deterministic=1`) changes neither the usual results nor the step
  time. A baseline comparison that names a scenario the change cannot have touched is how this shows: run
  that scenario again before believing it.
- **A console variable the engine does not know is accepted in silence.** `-dpcvars` creates a dummy
  variable and the run is unchanged, which reads like "this setting makes no difference"
  (`p.Chaos.Solver.SleepEnabled` instead of `p.Chaos.Solver.Sleep.Enabled` cost a wrong conclusion for a
  few minutes). `Tools\scenario.ps1` now prints `SCENARIO WARNING` for such a name.
- **PowerShell variable names do not tell case apart, and a typed parameter converts what is assigned to
  it.** In a script with a `[string]$Metrics` parameter, `$metrics = Read-JsonFile ...` stores the text of
  the object, and every field of it reads as empty. Give locals names no parameter has.
- **A passive body runs on the engine's 8 solver iterations; the NPC's 64 come from its controller.** A
  figure taken from a passive scenario is not the NPC's: hanging from one hand, the wrist stretches by
  2.9 cm at 8 iterations and 0.5 cm at 64 (`-CVars "p.Chaos.Solver.Iterations.Position=64"` to compare).
- **The engine's joint axes: twist is the frame's X, swing1 its Z, swing2 its Y, and limits are symmetric
  around the parent-side frame.** An asymmetric range needs that frame turned to the middle of the range;
  `FPhysicsAssetGenerator::ComputeJointFrames` does it. The two swings are limited each by itself, as a box,
  not together as an ellipse: the engine's joint solver (the linear one, which every joint uses unless told
  otherwise) takes the turn apart into a twist and a swing and limits the swing's two angles separately
  (`FPBDJointCachedSolver::InitPyramidSwingConstraint`). The project believed in the ellipse from M2 to M3
  part 5, and added the collarbones for it.
- **`FConstraintInstance::GetCurrentSwing1`, `GetCurrentSwing2` and `GetCurrentTwist` are not the angles the
  solver limits.** They are the twist of the whole turn about each axis. Compared with the limits they read a
  wrist that swings and twists at once as 40 degrees outside a range it was inside of, and that made hard
  limits look as soft as soft ones. `FSandboxPoseRig::LimitExcessDeg` measures as the solver does; a run's
  `metrics.worst_limit_angles_deg` and `worst_limit_ranges_deg` show the joint that was furthest out. When a
  figure does not move with the setting that should move it, suspect the figure.
- **A soft joint limit of the stock asset is not soft.** Stiffness 50 and damping 5 in the engine's
  acceleration mode hold a joint within 1 to 2.5 degrees of its range in a knockdown; a hard limit holds it
  no better and bounces (46 J gained against 26, rest after 4.0 s against 3.6). What a joint does at the end
  of its range is written in the spec (`"limit"`), not taken from a template without knowing.
- **The pose of a fall is for meeting the floor, not for lying on it.** Held until the body was at rest it
  made a stiff shape of the body: knocked over by the head, it stood on its head with its legs in the air for
  a second, and on its back it kept its forearms up. The muscles let go `thresholds.impact_let_go_s` after the
  trunk or the head first meets the floor.
- **Continuous collision detection does not keep a heavy body on light feet out of the floor.** A passive
  body dropped from 1.5 m sinks 8.3 cm in and is thrown back out with 149 J, with the feet swept or not; with
  64 solver iterations instead of the engine's 8 it sinks 1.5 cm. It is the solver not converging, not a body
  passing between two steps. A body under control has the 64.
- **Do not judge "at rest" by the fastest body.** A hand trembling against the floor (0.5 kg) kept a lying
  character "falling" for good. The controller uses the mass-weighted root mean square over all bodies.
- **Scenario time and controller time differ by one frame:** the controller's first tick comes one frame
  after the body is spawned. Result files use the run's own frame count; do not mix the two.
- **A bash heredoc breaks on an apostrophe in its body when run through the agent's shell tool,** and one
  that survives has its backslashes halved (`\\n` arrives as `\n`, `\t` in a path as a tab). Write the
  script to a file with the file tool and run that.
- **Chaos ignores new solver iteration counts on a body whose simulation island does not change.** The
  island's counts are worked out only when something joins or leaves it (`FPBDIslandManager`; the engine
  source has a todo about it). Set before the first step they apply; set on a standing character they were
  silently dropped: same hashes as without the change. `USandboxBodyController::RefreshSolverIsland` adds
  a constraint that restricts nothing for one frame, which counts as a change. Scenario `tune_live_solver`
  guards it.
- **A tuning change that "applied" has not necessarily reached the solver.** The case above was accepted by
  every layer of our code. When a value becomes tunable at run time, change it in the middle of a run and
  check that the hashes differ from the unchanged run.
- **The JSON reader takes a number for a boolean and a boolean for a number.** `TryGetBool` succeeds on `1`.
  Where the kind matters (scenario expectations, tuning values), check `FJsonValue::Type`.
- **Automation tests fail on any logged error.** Code that tests call must hand its errors back
  (`FString& OutError`) and leave logging to the caller, as `FSandboxParamStore` does.
- **Regenerating a map discards its baked lighting,** and the bake re-saves the map. So map generation is
  skipped while the map is newer than its spec, and the bake keeps a `.bake.json` stamp beside the map to
  know whether it is current. File times alone cannot tell.
- **The first lighting bake on a machine takes about 90 s, later ones 12 to 17 s** (Preview quality, the
  test room).
- **`-nohmd` does not stop the OpenXR plugin from creating an OpenXR instance at start-up.** With SteamVR
  as the runtime and no headset on, that call took 26 s per launch and ended in "Instance is not viable".
  So every tool launch passes `-nohmd -DisablePlugins=OpenXR` (`Invoke-Editor` in `Tools\common.ps1`); a VR
  run must leave both out. The plugin stays enabled in the `.uproject`.
- **Do not switch anti-aliasing back to TSR for VR.** At the headset's render target it cost 8.5 ms by
  itself and halved the frame rate. Renderer experiments go through `Tools\scenario.ps1 -VR -CVars "a=1,b=2"`,
  and every rendered run records the deciding console variables in `metrics.json` (`renderer`).
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
- **A substep in which a body sleeps has no contact event.** Contact events exist only for contacts that
  carried an impulse in that substep, so anything added up from them is short by the substeps slept
  through. See the pitfall about the solver's sleep.
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
- **What a strike does is over long before a run's last frame.** `-Shot` alone shows a standing character.
  Use `-ShotAt` with the times of the hit, and `-Overlay all` to see the contact and the flinch.
- **Overlay text reaches screenshots through the HUD's actor overlays.** With `AHUD::bShowOverlays` set and
  the actor added by `AddPostRenderedActor`, `DrawHUD` calls the actor's `PostRenderFor` with the scene
  canvas, whatever class the HUD is (the engine's plain `AHUD` in a scenario run, `ASandboxHUD` in a
  session). `AHUD::OnHUDPostRender` hands out the debug canvas instead. The lines are a `ULineBatchComponent`
  the overlay owns, emptied and filled again every frame, in the foreground depth group so that they show
  through the character.
- **The character's bodies do not answer the visibility channel** (the engine's `Ragdoll` profile ignores
  it). Aim traces go by object type (`ASandboxPawn::TraceAim`).
- **With a fixed timestep the engine does not limit the frame rate.** An interactive session would run as
  fast as the machine allows. `USandboxPlaySubsystem::HoldFrameRate` waits each frame out; it is the one
  place that reads the clock, and it must stay outside simulation code.
- **A pause set during a frame takes effect on the next one.** The pawn decides at the end of its tick
  whether the next frame steps. The engine also stops updating the camera of a paused game unless the
  player controller has `bShouldPerformFullTickWhenPaused`, and input actions need `bTriggerWhenPaused`.
- **A static mesh component with the engine's basic sphere did not become a simulating body here** (valid
  body, `IsInstanceSimulatingPhysics` false; the cause was not chased). Hands are `USphereComponent`s with a
  mesh attached for looks.
- **A sustained push by a hand made the character flinch again every time a flinch ended** (0.35 s), until
  it fell: each contact frame added to the felt impulse. Since M3 part 3 only the impulse beyond the
  smoothed pressure is a hit (`reactions.pressure_s`): one flinch as the hand lands (`effector_hold_chest`).
- **Add a tuning name and its value in the same build.** A build that does not know a name refuses the
  file, so a new tuning file dropped in while an older build is running a suite fails every active scenario
  after it ("is not a tuning value").
- **Scripts target Windows PowerShell 5.1.** No `&&`, ternaries or null-coalescing.
- **`| Select-Object -First N` stops the script that feeds it.** `suite.ps1 -SaveBaseline x | Select-Object
  -First 15` ended the suite after 15 lines, with exit code 255 and no baseline. Redirect to a file and filter
  that (`*> file; Get-Content file | ...`).
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
  (`balance.ankle_side_gain` is 0). A parallelogram lean (ankles and hips together) did the same.
- **Leaning into a held push needs the ankle targets to tilt.** Moving only the strategies' reference and the
  assist's anchors left the ankle muscles holding the upright pose: the assist carried the whole 40 N push.
- **A filter that lets a blow count as pressure counts its momentum twice.** A blow's momentum is in the
  capture point already. The lean and the decision use the force felt in every frame for
  `reactions.held_push_s` (0.3 s); a shove of 0.1 s is not a held push.
- **A hand held at a point pushes harder the more the body leans into it.** Moving the target 4 cm out lowered
  the force only from 138 to 104 N. Test a lean with a scripted force (`impulse` with `duration_s`).
- **A falling body kept reaching for a placed foot's point** and gained 1.7 J; the controller now lets placed
  feet go (eased) when it falls.
- **`Sandbox.Tune` with a value out of range changes nothing, and the run goes on.** A scenario's console
  event did that (`step_reach_cm=500`, range 300): check `control.tuning_changes` after such an event.
- **Physics Control damps a muscle against the target pose's own turning speed** (`AngularTargetVelocityMultiplier`,
  1 by default). A target that jumps in one frame is then a blow: a leg's target that moved 20 cm at lift-off put
  600 J into the body and snapped the calf at 84 rad/s. The muscles' `pose_velocity_share` is 0; only the swinging
  leg, whose target is a smooth path, follows its target's speed (`balance.swing_pose_velocity_share`).
- **A leg's target must never jump, whatever changes under it.** Every change of a foot's point or of the frame
  its leg is solved in glides from what the leg was solved for last (`FFootPlacement::GlidedPoint`,
  `FSwing::StartOffset`), not from where the foot happens to be.
- **The first scenario an engine process runs differs bit for bit from every later one,** and later ones do not
  depend on what ran before them. The job queue (`-ScenarioQueue`, `UScenarioBatchSubsystem`) therefore runs
  `Tools\batch_warmup.json` first in every process, and a single `scenario.ps1` run is a queue of one job; with
  that, 48 scenarios gave the same hashes in one process, in eight and in twelve.
- **A foot that has just landed stands on an edge.** The decision took the edge for the whole support and ordered
  a second step after a good one; while the foot settles (`balance.step_settle_s`) its whole sole counts.
- **Braking a stagger through the planted legs lifts the near foot** when it is strong (`balance.stagger_damping_s`
  0.2 against 0.15): the body rebounded off the new support and stepped again.
- **A body that decides it cannot be saved goes over like a plank.** Until the audit of November 2026 a capture
  point beyond one step's reach meant Falling at once: the muscles let go and the body tipped over its heels
  without having moved a foot, also after an 80 N*s push backward that two steps catch. It now always steps, and
  falls when the pelvis tilts or sinks (`thresholds.fall_*`).
- **Passing every check is not moving like a person.** The suite and the push matrix were green while the body
  stood with its arms held out, fell like a plank and stayed frozen in a lunge. Look at the motion
  (`Tools\film.ps1`: one sheet of pictures over time) before calling a behaviour done.
- **A falling body needs a pose of its own and legs that are let go in time.** Holding the stance, it fell
  straight; with a fall pose but its placed feet released over `poses.blend_s` (0.4 s), a body that fell after a
  step met the floor with straight legs. Placements are released over `poses.fall_blend_s`.
- **"Lying" must not wait for "motionless".** With the old rest thresholds (0.05 m/s for 0.5 s) the fall's pose
  kept the body curled up on its back, rocking, and it came to rest after 3.5 s; lying as soon as it is down and
  nearly still (0.3 m/s, 1 rad/s, 0.2 s) it rests after 1.7 s.
- **The collarbones are trunk, not arm.** In the arms' group, with arms soft enough to give way to a hit
  (40 N*m/rad), each collarbone sagged 10 degrees under its arm and laid the arm against the body.
  `FSandboxBodyTuning::GroupOfBone` puts them with the spine.
- **The muscles of a falling body do work** (10 to 30 J to fold the legs and put the arms out), so
  `max_energy_gain_j` of a fall is no longer near 0; the falling scenarios allow 40 J. Upright it is still 1 J.
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
- **A motion of a few centimetres does not show on a sheet of pictures.** The idle moves the hands by 4 to 7 cm
  and the head by 4; the film's tiles of a standing body with and without it look the same. Such a motion is
  measured (`body.bodies[].settled_travel_cm`, `metrics.settled_head_travel_cm`) and held by a check
  (`min_settled_head_travel_cm`); the look of it is for the owner in a session.
- **A body that moves by design needs limits that say so.** With the idle, `stand_npc` broke its limit on the
  fastest body after settling (0.05 m/s, written for a statue; the hands go at 0.08) and `pose_lean` its
  `min_pose_offset_deg` of 29.9 for an elbow asked for 30 (the idle turns it by up to 3). Jitter is still
  watched by the angular speed and the joint speed.
- **The idle clip is read from its keys, not from its compressed data** (`FSandboxIdleMotion::Load`): the editor
  builds the compressed data in the background, and a run must not depend on whether that has finished. The
  clip is Epic's content, fetched and not committed, and so is nothing derived from it.
- **The solver's determinism mode does not make a falling body repeat on a busy machine.** Chaos rebuilds the
  structure it finds colliding pairs with in a background task and takes it in on whichever step the task is
  done by; a pair, and with it a contact, then comes a step earlier or later. With 16 processes side by side
  3 to 18 runs of 100 of a scenario with a fall differed (always the same few other hashes), none in a single
  process, and no standing body ever did. `FScenarioStage::KeepSolverInStep` waits for the rebuild before every
  substep and lets none run during it. A run that differs "once" is a defect to measure, not to rerun: the same
  scenario 160 times side by side tells in a minute.
- **Moving a file is not a way to take a job from a shared queue.** Windows renames through a handle; two
  processes can each open the file before either renames, both renames succeed, and neither process can then
  read "its" file. About one job in a thousand was run by nobody. A job is taken by writing a marker while a
  lock file is held open for writing (`UScenarioBatchSubsystem::TakeJob`), and a job without a result now prints
  `QUEUE WARNING` and keeps the processes' logs under `Saved\Queue`.
- **Letting a clip run further ahead of the body leaves less room before the body counts as lost.** With the
  pelvis allowed 16 cm of height and 20 cm of floor behind the clip instead of 12, the stand went from 45 to 48 of
  50, and a 30 N*s push that the get-up had ridden out now put the pelvis 31 cm off, past `getup.lost_cm` at 30:
  the suite's `getup_push_weak` caught it. The two limits move together (lost is 40 now, and the 200 N*s blow still
  fells the body at 45).
- **A figure from one seed is luck.** With the clip waiting for a pelvis 12 cm off in any direction, the stand
  gave 48 of 50 on seed 1, and 45 and 45 on seeds 2 and 3: on the criterion's edge. A setting is chosen on three
  seeds (`Tools\getup-stand.ps1 -Seed`), and a single failed fall that changes with a setting is no finding.
- **A clip must not wait without end for what will not come.** A foot that lay across the other leg was 29 cm
  from where the clip's first key had it and off the floor; the clip waited for it through six attempts. The wait
  for a foot is bounded (`getup.foot_wait_s`); the pelvis's height is what a get-up really has to wait for.
- **The same clip from the same place fails the same way.** A body that gave a get-up up tried again by the same
  clip, five and six times. The next attempt takes another clip if one starts at all like the body lies.
- **A body knocked into a wall often does not fall.** Three of eight blows toward a wall 45 to 90 cm away ended
  with the body on its feet against it. A stand of falls draws such a trial again and says how many it drew.
- **A body appears in its reference pose, the arms out, and brings them down in half a second.** The fingertips
  are 57 cm to either side of its axis, at the height of a chair's back. Put 45 cm from a thing at its side, a
  finger appeared 0.12 cm inside the chair and 0.79 cm inside a wall, and no check said so. A thing beside a body
  is 65 cm away or more; `Tools\getup-stand.ps1` refuses a place where the arms would touch
  (`penetration.spawn_world_cm` of a run shows it: 0.034 is the soles on the floor).
- **`body.bodies[].final_location_cm` is a body's centre of mass from where the character was spawned, not a place
  in the world.** Read as world coordinates in a room, it put eleven bodies "standing inside the table". World
  positions over a run are in `trace.json` (bone origins, every 0.5 s).
- **A get-up that is given up moves the body.** Each failed attempt shifts it by half a metre; a body that fell at
  a wall 166 cm from the table stood, at the fourth attempt, 30 cm from the table. A fall "at a wall" stays one
  only by distance: the stand has those falls in the room's corner quarters and reports how near each came to
  the furniture.
- **A clip that stays where it was put makes the assist drag the body over the floor.** A body that turns into a
  clip's first pose, rolls over or comes onto hands and knees moves 20 to 35 cm, and the clip waited there for the
  pelvis to be pulled back: half the assist's force went into that, and with a weaker assist the clip waited for
  good. The clip's root follows the pelvis while the feet carry nothing (`getup.pelvis_follow_s`). The assist was
  believed to be lifting a body whose arms were too weak; the log line of the failed attempts said otherwise
  (pelvis 30 cm off over the floor, 4 cm in height).
- **An assist that is capped always pulls at its cap,** so its share of body weight measures the caps and not the
  need. Sweep the caps down against the stand and read what fails, not the share.
- **A scripted hand goes to a point of the world and slides off a body that moves.** Of the two hands of
  `getup_held` one ends on the floor; "two hands only slow a get-up" was measured on one hand from part 6 on, and
  a threshold set between "one hand" and "two hands" from those runs separated nothing. A hand's sustained push
  also reads above its spring's cap (up to 342 N for 300 N: its weight and the body's push against it). Read
  `effectors.list.<hand>.touched_impulse_ns` before believing which hand pressed on what; a press of a known
  size is a scripted force (`impulse` with `duration_s`).
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
- **"The elbow cannot straighten" was written down and believed for two parts; the joint can.** The stance never
  asked. Before planning a change to the asset for something a joint "cannot" do, ask it for the pose
  (`-Tune "poses.stance.<joint>.swing1_deg=..."`) and look.
- **A clip steps a foot that the body is standing on.** A recording brings a foot forward while its weight is on
  the hands or the other leg; the simulated body's weight is where its own feet, hands and friction put it. With
  the clip's time waiting only for the pelvis, the body stood up leaning like a plank from a foot 45 cm behind where
  the clip had it; made to wait for the feet too, it waited for good, at 16 cm, for a foot that carried 525 N. A
  foot the body stands on is where it is: the clip's root follows the loaded feet (`getup.frame_follow_s`), and the
  clip waits only for a foot that is off the floor.
- **The assist of a get-up takes back a blow unless it is capped and the body may be lost.** With 500 N on the
  pelvis and `getup.lost_cm` at 45, a 200 N*s blow moved the body 40 cm and the assist had it back in half a second
  with 1.2 times its weight. Capped at 250 N (feet 50 N) and lost at 30 cm, the same blow fells it. Sweep the caps
  down to what the body still gets up with; the share is in `control.get_up`.
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
- **A contact's friction is not the body's.** The engine combines the two surfaces of a contact, by the average
  unless one of them asks for a mode with a higher number (the smaller, the product, the larger), and the floor
  and the hands have the engine's 0.7 and 0.3: `surface.friction` 0.3 is 0.5 on the floor, and under the average
  no value of the body gives less than 0.35. A material's "override combine mode" flags are read by nothing. What
  the floor gives is measured (`Tools\surface-friction.ps1`), and a new value of a material reaches the solver
  only through `FChaosEngineInterface::UpdateMaterial` (`USandboxBodyController::ApplySurface`).
- **A passive body keeps the engine's default surface.** The surface values are the controller's; `drop_*` and
  `impulse_passive` slide and bounce on 0.7 and 0.3.
- **More grip is not more stability.** With the soles at 0.9 on the floor a blow of 200 N*s on a thigh tripped
  the body where it had stumbled and stood, and after a push sideways it froze on crossed legs, in Stagger, for
  the rest of the run; the push matrix called both "stepped" or "fell" as allowed and passed. And a body with 0.9
  against the floor gets up worse: turning into a clip's first pose and coming onto hands and knees it has to
  slide.
- **"In motion" by the mass-weighted speed lasts long after a body has stopped:** limbs go on settling above the
  threshold. Friction worked out of the time a dragged body was in motion read 0.55 where the acceleration gave
  0.69. Check a new measure on a case whose answer is known.
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
- **An engine process takes 1.75 GB.** Fifteen side by side (three stands at once) took 26 GB of 32 and the
  batch was stopped for want of memory. Nine is the most that run at a time (`Tools\getup-seeds.ps1`).
- **PowerShell hash keys do not tell case apart either.** `@{ s = 0; S = 0 }` does not parse, and a script that
  does not parse fails at once: run a new helper once in the foreground before a long batch in the background.
- **A run that only asks whether the body ends standing need not run to its end.** Most bodies of the stand are
  on their feet 8 s after the blow and were simulated for 40 s. `end_when_standing_s` ends a run once every event
  is over and the body has been Standing for that long without a break; checked against the full-length runs
  trial by trial before it became the default.
- **A `TUniquePtr` to a type that is only declared does not compile in a `UCLASS`** (C4150 from the generated
  code); `UScenarioBatchSubsystem` owns its log device through a plain pointer.
- **A count that starts again whenever there is progress does not bound a wait.** `getup.stall_s` (3 s) counts
  while the clip's time stands still; a body that hovers at the lag lets the clip move on by a frame now and
  then, and an attempt lasted 15.6 s, 13 of them in one pose. A wait is bounded by a total (`getup.wait_s`). When
  a limit "cannot" have been passed, look at when its counter is set back.
- **A gate's figure is measured at the gate.** "Under 300 N two bodies of three get up" was measured in step 1 of
  part 7 and carried to the gate; step 2 changed the body's friction and nobody ran the pressed stand again: it
  was one of three. A change to the body is followed by every measurement that a written claim rests on, not only
  by the suite.
- **A scripted force is felt as a press; a pin is not.** `impulse` with `duration_s` goes through what the body
  feels (`SustainedN`), so a get-up under it is "pressed" or "held down". To keep a body from rising without
  pressing on it, pin it (`getup_wait_bounded`).
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
- **A run in the headset that has to start SteamVR itself rebooted the machine.** On 24 November 2026 the owner
  had closed SteamVR and left; a `-VR` launch 13 minutes later started SteamVR through the OpenXR instance, the
  engine created its D3D12 device while SteamVR's compositor was coming up, and the NVIDIA driver took the system
  down (`nvlddmkm.sys`, bugcheck 0x3B, `Kernel-Power` 41; the engine's log ends between "Emitting draw events
  for PIX profiling" and "Aftermath enabled"). Every uncommitted change was open; none was lost. The scripts now
  refuse a `-VR` run unless `vrserver` and `vrcompositor` are running, the compositor has been up for 30 s and
  SteamVR's own log says ready or standby (`Get-HeadsetBlocker` in `Tools\common.ps1`), and say when it is
  standby (nobody wears the headset). **A headset run is
  started only when the owner has said, in that message, that the headset is on and SteamVR is up; never in
  unattended work.** Before a run that opens the GPU in unattended work, copy what is not committed
  (`Saved\Backups\backup.ps1`). The dump (`C:\Windows\Minidump`) needs an administrator to read.
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
- **Two bodies that stand for one hand have other shapes, and one put where the other lies is inside what that
  one touched.** A ball where a plate lay flat on the chest is 3 cm in the chest. The hand changes its body only
  where the other has room (`WouldOverlap`), and a grip that closes on something is counted, not made a fist.
- **A flat face turned from the stage's one light is black on black.** The open hand's plate did not show on a
  scenario's picture at all; the overlay's hands layer draws it as its edges (`-Overlay hands`). Before
  concluding that a thing is not there, draw it.
- **A hand has to be able to reach the speed a scenario asks for.** With the spring's cap of 300 N a target that
  goes 60 cm at 15 m/s is there and back before the hand has moved: no contact, no impulse, "no tunnelling". A
  sweep of speeds raises the cap (6000 N in M2 and here) and reports the speed the hand reached.
- **The engine makes a controller's vibration last one frame** (`FOpenXRInput::SetHapticFeedbackValues`: its
  duration is the frame's own), and hands the frequency on as hertz. A vibration that is to go on is asked for
  in every frame; asked for once when its strength changes, a held press is silent after 11 ms. Found by a
  reader of the code, not by a test: nothing here can feel a controller.
- **After a night of unwatched changes, have the diff read by someone who did not write it,** and give them the
  code, not the account of it. Two real defects of 41 files came out that way, both in paths no test reaches.
- **A skeletal mesh answers an overlap query as one thing.** `FOverlapResult::ItemIndex`, by the engine's own
  comment the body's index, came back -1 from `OverlapMultiByObjectType`. To learn which body of a character is
  near, ask each (`FBodyInstance::OverlapTest`), as `USandboxEffectorComponent::FindGrabbable` and the runner's
  hand probe do.
- **A joint's "force" from the engine is its impulse over the last substep** (`FConstraintInstance::
  GetConstraintForce`): divide by the substep. Calibrated on a hand that drags at a steady 231 N of its spring
  (124 kg*cm/s at 180 Hz). A comment in the solver speaks of force times the step squared; the number does not.
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
- **The engine keeps a joint's force for the last substep of a frame only.** A largest force read once a frame
  misses a jolt in the first of two substeps.
- **A test for "clear of everything" that leaves a whole actor out leaves out the other hand.** Two hands are
  one actor; a hand that had held something was set back to colliding while it lay inside the other one.
- **A scripted hand can be sent to a part of the body instead of a point of the world** (`"at_bone"` on an
  `effector` event: the keys after the first are measured from where that body is when the event begins). A
  scenario that starts with a fall should use it, and so should any measurement on many falls.
- **What a source decides about an effector belongs in its target.** The desktop's foot was made weak at rest
  by the pawn setting the effector's strength from outside: the recording did not have it, and a replay's foot
  had 1800 N where the session's had 40. The bootstrap's record-and-replay step caught it. The strength is a
  field of `FSandboxEffectorTarget` now, and a number of a recorded target.
- **A body that is made weak while it still moves keeps moving.** The foot came home from a kick at 8 m/s and
  was given its resting 40 N on the frame its target arrived: 40 N*s against 40 N. It keeps its strength for a
  settling time. The same for a target that jumps: a weak body drifts to it through what stands between.
- **A limit that a missing thing meets is no check.** `max_prop_travel_cm` passed for a prop that was never
  made (it had travelled 0 cm). A run with props checks that they are there (`props_simulated`).
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
- **Two runs that differ in two things say nothing of either.** "The same kick is seven steps on a body that
  stays upright and a fall on one that folds over it" was written from a run of the night and one of the
  morning; the two feet had other springs (16000 and 24000 N/m). With the reaction switched off on the morning's
  foot the body fell as well. To say what a change does, switch that change alone (`-Tune`).
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
- **A session with hard blows is not reproduced by its replay.** `vr_check3` replayed ended 153 cm from where
  the session did, with none of its eight falls; `vr_check2`, with weak punches, to 0.00 cm. What happened in
  such a session is in the session's own log, not in `Tools\recording-report.ps1`, which reads a replay.
- **A recording replayed with other hands measures nothing after its first hit.** The body stands elsewhere
  from then on and the recorded hands strike the air (9 hits of 51).
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
- **A contact event has one point of the contact, the nearest, with the impulse of all of them.** A foot's centre of
  pressure made from the events' points lay 8.6 cm behind the body's centre of mass. Where a force goes through is in
  the solver's manifold points with their own impulses, after each substep (`FNaturalMeasures::AddPressure`); the last
  substep alone will not do either.
- **The solver moves the bodies after its drives.** Joint projection and unfinished iterations turn a joint by 0.2 to 0.35
  degrees after the drive has acted, so a torque worked out from the spring law is off by about 0.005 rad times the
  stiffness: 4 N*m at 800 to 1200 N*m/rad. With 200 position iterations and no projection the law reads the known
  answer to 0.5%. Projection conserves no momentum: it is why the floor's push on a standing body adds up to 0.995 of
  its weight.
- **`Sandbox.Tune` takes all of its values or none**, and says so only in the log: a check scenario whose one value was out
  of range ran the untuned body and measured that. A scenario that tunes expects `tuning_changes`.
- **`@(, @(1, 2), @(3, 4))` is not a list of two pairs in PowerShell.** The comma binds to the first item only and wraps it
  once more; the first pair of every such row was never judged. And `Measure-Object` over the `$null` that an empty
  list becomes reads 0, which is within most limits: say what a figure that is missing means.
- **A measure can be met by what it was not meant for.** "A hand touches before the trunk" read 100% of forward falls: the
  hands land beside the hips and the head strikes the floor; asked for a hand ahead of the chest, it still read 86%. Look at
  the motion (`film.ps1 -Around`) before a criterion that is met is believed, and run a sharper rule before saying what it
  will read.
- **A check that a thing is its own inverse does not say it is right.** A pose database mirrored without changing sides
  passed "mirrored twice it is itself". Break a measure on purpose and see its check fail before relying on either.
- **The MetaHuman's corrective blueprint runs on our body.** "No Animation Blueprints" was read as "none is there", and the
  plan said the skin had no correctives. The assembled mesh brings `ABP_Body_PostProcess` as its post-process blueprint and
  the engine runs it. It sees the pose the muscles are asked for, not the simulated body, and switching it off changes every
  hash by rounding. Ask the bones (`trace.json`, with the mesh's bone names from an editor script) before saying what moves them.
- **A glide that is cut short has to be gone on from where it was cut.** `PlaceFoot` without `bFromLast` starts a change of
  frame from the frame the leg was going to, not from the one it was last solved in: 21 cm, 27 degrees of the thigh in one
  frame after a lift (known, to be repaired: `docs/natural-body.md`, decision 13).
- **The largest change of a command in a frame is not its largest jump.** A swinging leg's is 65 N*m, and 4 of them are a
  jump: read `command_jump_max_nm`. And a frame can have several causes: of 365 N*m 333, 333 and 140 were left with each of
  three put right alone, 26 with all.
- **What held the head back from turning was the world.** "A slap does not turn the head because the neck is stiff and the head
  round" was written on 25 November; over necks of 40 down to 1 N*m/rad the turn went from 6 to 15 degrees only. The assist's hold
  on the head (10 N*m/rad, 0.2 s: a damper of 2 N*m*s/rad on 0.02 kg*m2) took most of it. When a figure hardly moves with the
  setting that should move it, list every force on the body, the world's among them.
- **A test that strikes at a point of the world is written for the shape that was there.** The slap's path ended 4 cm inside the
  old head; the new, narrower head met it at the end of its path, at 1.4 m/s. And the plate of the open hand reaches 18 cm past the
  hand's own point: held with the fingers forward, it met the face with its rear edge. Change the body and these change with it.
- **Every tool runs on another physics asset without a file changed:** `SANDBOX_PHYSICS_ASSET=PA_SandboxNPCHead` (and
  `SANDBOX_HEAD_BODY=head` for a body whose head is its own) puts that asset into a copy of each scenario in the queue.
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
