# Scenarios

Each `*.json` file here is one reproducible physics run. `Tools\scenario.ps1 -Name <file name without .json>`
runs it at a fixed timestep with no renderer and writes `Saved\Scenarios\<name>\metrics.json`, `trace.json`
and, for a simulated body, `series.csv`.

The file name must equal the `name` field (a unit test checks this).

## Format

```json
{
  "name": "impulse_passive",
  "duration_s": 10.0,
  "fixed_fps": 90,
  "checkpoint_interval_s": 0.5,
  "body": {
    "mesh": "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple",
    "physics_asset": "",
    "location_cm": [0, 0, 150],
    "rotation_deg": [0, 0, 0]
  },
  "events": [
    { "t": 4.0, "type": "impulse", "body": "pelvis", "impulse_ns": 80, "direction": [1, 0, 0.3] }
  ],
  "expect": {
    "rest_within_s": 9.5,
    "max_joint_stretch_cm": 2.0
  }
}
```

| Field | Required | Meaning |
| --- | --- | --- |
| `name` | yes | Scenario name; equals the file name |
| `duration_s` | yes | Simulated time |
| `map` | no | Map package to run in, e.g. `/Game/Generated/Maps/TestRoom`. Without it the runner builds a bare floor and two lights |
| `fixed_fps` | no, default 90 | Engine frames per simulated second. Physics substeps come from `Config/DefaultEngine.ini` |
| `checkpoint_interval_s` | no, default 0.5 | How often body and bone transforms are written to `trace.json` |
| `end_when_standing_s` | no | The run ends before `duration_s` once every event is over and the body has been in the Standing state for this long without a break (`timing.ended_when_standing`, `timing.frames`). For runs that only ask whether the body ends on its feet, as the stand of get-ups does: `duration_s` is then the time allowed for the slowest. Active control only; the hashes are those of the frame the run ended on |
| `pose_prior` | no | A pose database (`Tools\pose-prior.ps1 -Build`; a path, relative to the repository unless absolute) that the body's poses are held against: `metrics.pose_prior`. Not for a scenario of the suite: the database is not committed. `pose_prior_clips`, a list of animation paths beside it, makes the run build the database first |
| `body.mesh` | yes | Skeletal mesh object path |
| `body.physics_asset` | no | Overrides the mesh's own physics asset |
| `body.location_cm` | no, default `[0, 0, 150]` | Spawn location of the mesh origin (the feet). The floor's top is at z = 0 |
| `body.rotation_deg` | no | `[pitch, yaw, roll]` |
| `body.attached_meshes` | no | Extra skeletal meshes that follow the body's pose and have no collision (a MetaHuman face) |
| `body.simulate` | no, default `true` | `false` keeps the body kinematic in its reference pose, for portraits and rendering-cost scenes |
| `body.get_up` | no, default `false` | Whether a body that lies gets up by itself (`getup.*`). Off unless asked for: a scenario about a fall is about the fall, and its body stays where it came to rest, so that rest time and energy are those of the fall. In a session the body gets up |
| `body.control` | no, default `"passive"` | `"passive"` is a plain ragdoll. `"active"` adds muscles and balance assist, tuned by the files in `Config/Tuning`: the character holds the pose it is spawned in. Spawn it with the feet on the floor (`location_cm` z = 0) |
| `settle_s` | no, default 2 | Start-up time left out of the "settled" metrics and expectations |
| `camera` | no | `{ "location_cm": [x, y, z], "look_at_cm": [x, y, z] }` for rendered runs. Without it the runner frames the body |
| `events` | no | Scripted stimuli, applied at simulated time `t` |
| `expect` | no | Pass criteria; omitted ones are not checked |

Event types. Unknown types are a parse error.

- `impulse` applies `impulse_ns` newton-seconds along `direction` (normalised for you) to the body on bone
  `body`. Without `duration_s` it is a single blow; with it, a push: the same impulse delivered as a steady
  force over that time. An actively controlled body feels the hit (`USandboxBodyController::ApplyImpulse`)
  and may flinch; a passive one just receives it. This is the calibrated stimulus: it acts at the body's
  centre of mass and touches nothing. The impulse tool of an interactive session uses the same entry.
- `pin` fixes the body on bone `body` at its current world position with a ball joint; the rest of the
  character hangs from it. `metrics.max_pin_error_cm` reports how far the body strayed from the pin point.
  With `"lock_rotation": true` the body's orientation is held as well (a weld): `pose_foot_place` holds
  the pelvis that way, so that a leg can be placed without the body having to balance. With `"follow_clip":
  true` as well, the weld goes to where the clip whose pose is asked for (`clip_pose`) has the pelvis, raised
  by `raise_cm`: the body is laid down, turned and stood up as the clip has it, clear of the floor or on it.
  The mesh's own space at the time of the pin is taken for the clip's root, so pin at `t` 0. This is a rig for
  looking at a clip (`Tools\clip-film.ps1 -Carry`), not a way to move a body: a weld gives way to nothing, and
  a body carried through a get-up on the floor is pressed up to 10 cm into it.
- `clip_pose` asks the body's muscles for the pose a recorded clip has at `time_s` (`clip`, the path of an
  animation sequence) and holds it; the target goes over within `poses.blend_s`, from the body's own pose or
  from the clip pose asked for before. With `"rate": 1` the clip's time then runs on as recorded, up to the
  clip's end. `"release": true` (and nothing else) lets the pose go again. The clip may be made for another
  skeleton with the same bone names (`FSandboxClipPose`). While a clip's pose is asked for the body leaves
  its balance alone: no step, no fall by its posture, no idle. Hold the body (`pin`) or expect it to go
  over. `control.clip_pose` is the clip and time at the end, `control.clips` every clip read. Active control
  only.
- `foot_target` asks the body's target pose to put a foot (`body`, the foot's bone) at a point: `offset_cm`
  from where the foot is when the event fires, with `weight` from 0 to 1. The thigh and the calf turn
  (two-bone placement) and the foot keeps the orientation the pose gives it; the weight eases in over
  `poses.blend_s`, and 0 lets the foot go back to the pose. The muscles do the work; nothing moves the foot
  directly. `control.foot_targets` lists each placed foot with its distance from its point at the end
  (`final_error_cm`). Active control only.
- `console` runs `command` as if it were typed into the console of the running session, for example
  `"Sandbox.Tune muscles.legs.stiffness_nm_per_rad=0"`. It counts as applied when the engine knows the
  command; whether a tuning command changed anything shows in `control.tuning_changes`.
- `edit_tuning_file` writes the numbers in `values` (tuning name to value) into the tuning files and does
  nothing else: the running session has to notice the edit by itself, as it would after a save in an
  editor. A scenario with such an event runs on its own copy of the tuning files in `<Out>\tuning`, so
  `Config/Tuning` is never written to. Active control only.
- `effector` moves one of the player's hands (`"effector": "hand_left"` or `"hand_right"`) along a `path`.
  See below.

## Hands

```json
{ "t": 3.0, "type": "effector", "effector": "hand_right", "path": [
    { "t": 0.0,  "location_cm": [0, 70, 128] },
    { "t": 0.25, "location_cm": [0, 8, 128] },
    { "t": 0.5,  "location_cm": [0, 70, 128], "rotation_deg": [0, 0, 0] } ] }
```

A scenario with `effector` events has the hands a player has: the same bodies on the same springs
(`ASandboxEffectorRig`, tuned by `effectors.hand.*` in `Config/Tuning`), with their targets read from the
file instead of from a camera. Nothing tells the character about them. It learns that it was hit from its
own contact events, by the same rules as for an `impulse` event, and may flinch.

- A path is the **target's** way, not the hand's. Key times count from the event's `t`, the first is 0 and
  they increase. Between two keys the target moves in a straight line at constant speed, and that speed is
  handed to the spring, so a free hand follows without lagging. After the last key the target stays there.
- The hand itself is a 1.5 kg sphere pulled by a spring that can apply 300 N at most. It arrives where the
  target is only if nothing is in the way, and a target that jumps is followed as fast as that force allows.
- A hand named by any event exists from the start of the run, resting on the first key of its first path.
  A later path of the same hand takes over when it begins.
- `rotation_deg` is `[pitch, yaw, roll]`; a key without one keeps the rotation of the key before it.
- `grip` is 1 for a fist and 0 for an open hand; a key without one keeps the grip of the key before it, and a
  hand whose keys say nothing is a fist, the ball it has always been. An open hand is a flat plate in the hand's
  own frame (x along the fingers, z toward the thumb, the palm facing along y), shown always and, with
  `effectors.hand.palm_collides` set to 1, the body that collides. Unturned, a hand's palm faces along the
  scenario's y, toward or away from an unrotated character; `"rotation_deg": [0, 90, 0]` turns it to face along
  x, and `[90, 0, 0]` points the fingers up. A scenario switches the plate on with a console event at 0 s
  (`Sandbox.Tune effectors.hand.palm_collides=1`), as `palm_slap_head` does.
- In the scenario's own space the character faces +y and its left is +x, when it is spawned unrotated.

`effectors` in `metrics.json` has, for each hand: `contact_impulse_ns` (the sum of all contact impulses),
`touched_impulse_ns` (the same by what was touched: a bone of the character, or an actor's name),
`first_contact_t`, `contact_frames`, `max_contact_force_n` (the largest net contact force over one frame),
`final_contact_force_n` (the same in the run's last frame), `max_gap_cm` and `final_gap_cm` (distance from
the hand to its target), `max_speed_mps`, `max_demand_n` (the largest force the spring law asked for before
its cap), `snaps`, `shape_changes` (times the hand changed between the ball and the open hand's plate; the
expectation `effector_shape_changes` holds the sum over the hands), `grips_on_body` and `last_gripped` (times the grip closed
while the open hand lay on something, and on what: such a hand stays open until it has room to be a fist; the
expectation `effector_grips_on_body` holds the sum), `open_hand_at_end` and `mass_kg` of the body that is the
hand at the end. Of a grab (`effectors.hand.grab`, which a scenario switches on with a `console` event at
`t` 0: `Sandbox.Tune effectors.hand.grab=1`): `grabs` and `last_grabbed` (times a closing grip took hold of a
body, and of which), `grab_frames` (frames it held something), `grab_slips` (times the grip slipped because the
hand was left behind its target), `grab_overloads` (times it was torn open by its load), `grabs_lost` (what it
held ceased to exist), `max_grab_stretch_cm` (the furthest the two held points came apart), `max_grab_force_n`
and `mean_grab_force_n` (what the joint carried, in a frame at most and on average while it held) and
`grabbing_at_end`. The expectations `effector_grabs`, `effector_grab_slips` and `effector_grab_overloads` hold
the sums over the hands exactly, `min_grab_frames` the sum of the frames, `max_grab_stretch_cm` and
`max_grab_force_n` the largest of the hands. A hand that holds something collides with no simulated body, and
its penetration and tunnelling are not measured against the character for as long. What the character felt is in `control`: `contact_impulse_ns` and `contact_reports`.

A strike adds, per hand: `first_touched` and `first_contact_speed_mps` (what it touched first and how fast it
was moving at the start of the physics step in which it did), `struck_bone` and `struck_displacement_cm`
(the body of the character it touched first, and the furthest that body's centre of mass got from where it
was just before the contact), and what the next section measures.

### Recorded hands

```json
{ "recording": "Recordings/demo_hands.json", "recording_start_s": 0.66666666666666663 }
```

Instead of `effector` events a scenario can name a **recording**: what the hands of a session were asked for in
every stepped frame (`FSandboxTargetRecording`, a text file with one line a frame: place, turn, velocity and
grip of each hand). The path is relative to the repository unless absolute. The hands are the same rig; their
targets are read from the recording frame for frame, with nothing worked out in between, so the recording's
`fps` has to be the scenario's. A scenario has `effector` events or a recording, not both.

- `recording_start_s` is when the recording's first frame is used: for the physics step that begins that long
  after the start of the run. A session writes the age its character had when the recording began, so that the
  replay's hands meet a body of the same age (the idle moves the body by a few centimetres).
- Before that the hands wait on the first frame's targets, and after the last frame they stay on its targets.
- A recording is made of points of the world. Against a body that does not move as the session's did, the hands
  reach for where that body was.
- `effectors.recording` in `metrics.json` has the file, its frames and the frame it started on.

A recording is made in a session: `Tools\play.ps1 -Record <name>` records from the first frame, the K key ends
a recording and starts the next. Each is written to `Saved\Recordings\<name>\`: `<name>.recording.json`,
`<name>.json` (the session's scene with the recording, no events, no expectations, as long as the character had
lived when the recording ended) and `<name>.session.json` (what the session saw at that moment: the body's
states and their frames, where the pelvis was, what each hand had touched and with how much). To keep one, copy
the recording to `Recordings\` and write a scenario for it with expectations, as `replay_demo_hands` is.

A session writes its recording when it ends by Escape or by itself after
`-Seconds <n>` (simulated seconds); one that is killed or times out loses it. `play.ps1` says so when `-Record`
was asked for and nothing was written.

In a headset (`Tools\play.ps1 -VR -Record <name>`) the recording is made the same way: it holds what the hands
were asked for, whichever source asked, so a scenario that replays it needs no headset. The session file then
has, under each hand, `tracking`: `tracked_frames` and `lost_frames` of its controller, `estimated_frames` (lost
frames in which the runtime still gave a pose, not marked as tracked), `no_linear_velocity_frames` and
`no_angular_velocity_frames` (frames in which the runtime gave no velocity and the difference of two poses stood
in), `moved_without_velocity_frames`, `catch_ups` and `catch_up_frames` (the target on its way to a controller
that was found again or jumped), `max_target_speed_mps` and `max_grip`, all counted over the whole session up to
that moment.

A run with hands also writes `hands.csv`, one line a frame: for each hand its distance to its target at the
start of the coming step (`<hand>_gap_cm`), its speed, the speed its target was reported to have, the force the
spring law asked for before its cap, and the contact force of the step before. `Tools\swing-lag.ps1` reads it.
A hand that follows a moving target perfectly still shows one frame of the target's travel as a gap: it is given
the place for the start of a step and is there at its end.

**What the body told of.** `control.events` in `metrics.json` lists, in order, every hit and every change of
state the body told its listeners of (`USandboxBodyController::OnBodyEvent`): `t`, `type` (`hit` or `state`),
the `state` the body is in, and for a hit the `bone` and `impulse_ns`, for a change the `previous_state` and the
`reason`.

A scenario may put things with mass into its world: `"props": [ { "name": "box", "size_cm": [40, 40, 45],
"mass_kg": 5, "location_cm": [150, 60, 22.5], "rotation_deg": [0, 0, 0] } ]`. A prop may add `"handle_cm": [40, 0, 0]`,
a point of it from its middle that a hand takes it by when it closes near it (`effectors.hand.grab_handle_cm`;
a hand's `handle_grabs`, the expectation `effector_handle_grabs`). In place of `size_cm` it may list `"boxes": [ { "center_cm": [0, 0, 73],
"size_cm": [120, 70, 4] }, ... ]`: the thing is these boxes as one body (a table: its top and four legs), and `location_cm` is then its own origin, from which the boxes' middles are measured. Each is a box that is
simulated from the first step (`location_cm` is its middle; put it on the floor, not in it), collides with the
floor, the character and the hands, and can be taken hold of by a hand with the grab on (a hand's
`last_grabbed` then reads `Prop_<name>`). `props` in `metrics.json` has, for each: `mass_kg`,
`start_location_cm`, `final_location_cm`, `final_tilt_deg` (0 upright, 90 on its side), `travel_cm` (the
furthest it was from where it started) and `max_speed_mps`. Expectations: `min_prop_travel_cm` (of the prop
that went least far), `max_prop_travel_cm` and `max_prop_speed_mps` (of the one that went furthest and
fastest: nothing was launched), and `min_felt_impulse_ns`: the body felt at least that much from the
simulated bodies that touched it, hands and things alike (`control.contact_impulse_ns`), which is how a
scenario says that a thing reached the body at all. An event `prop_move` drives a thing: `{ "t": 3, "type": "prop_move", "prop": "plate", "velocity_mps": [0.3, 0, 0], "duration_s": 8, "ramp_s": 1 }` (by a strong spring, over the floor).
A run with props also checks that every one of them is there and simulated
(`props_simulated`). Props are part of a run's hash.

The effectors are `hand_left`, `hand_right` and the kicking feet `foot_left` and `foot_right` (heavier and
stronger, `effectors.foot.*`; a foot is always a ball and takes no `"grip"`).

An `effector` event may say `"at_bone": "spine_04"`: its keys after the first are then measured from where
that body of the character is (its centre of mass) at the moment the event begins, along the world's axes, and
the first key is where the hand waits until then, in the world. For a hand that has to find a body wherever a
fall has put it (`grab_getup_held`; `Tools\getup-stand.ps1 -HoldHands`). The body's place is read once, as the
event begins: let the body come to rest first. A later event of the same hand begins, as always, with a jump
of the target to its own first key, which is a place in the world whether or not that event has `at_bone`: write
it where the hand will be. A name that is no body of the character is an error in the log and fails the run
(`all_events_applied`), and the hand then goes to the keys as written.

A grab and its end are events too (`"type": "grab"` and `"release"`, with `bone` and `holder`, the
hand's name): the body does not feel a hold, it is told of it by the run, which knows both the hands and the
body. `control.grab_events` counts the grabs, and the expectation `"grab_events": 1` holds the count and that
what the body was last told is held is what the hands hold of it at the run's end. `control.hit_events` counts the hits, and the expectation `"hit_events": 1` holds the count. A hit is
one contact that has brought what makes a standing body flinch: a hand that lands and keeps pressing is one, and
a blow that lands while something is pressing is another.

**Takes.** The N key of a session (in the headset: both fists raised above the eyes with the grips held closed for a second) begins a new take: the
recording that runs is written, the character is put back, the hands are made anew on their targets, and a new
recording begins, written as `<name>_2`, `<name>_3` and so on. Each take's scenario starts with a fresh body and
its recording at 0 s. A take's replay ends as the take did within limits, not to the digit: see
`docs/testing.md`. `Tools\record-replay.ps1 -Takes` checks three takes made without a person.

`Tools\record-replay.ps1` is the check of the whole way: a session without a person uses the hands alone
(`play.ps1 -Demo`: the right hand reaches into the chest, then the left), the recording is replayed as a
scenario, and the replay is held against the session file: the same states, each within a frame of the
session's, the pelvis within 1 cm, each hand's impulse within 5%, and with `-Twice` two replays with the same
hashes. A session and a scenario are different runs (a session's process runs no warm-up, its hands exist from
its first frame), so they agree to the first contact and by figures after it, not bit for bit.
`-Recorded <name>` checks a recording a person made. The drag handle, the impulse tool and the reset are not
recorded: a session that used them will not replay to the same end. Pause and slow motion do no harm: only
the frames on which the world stepped are kept.

## Tunnelling and penetration

The solver's contacts cannot say whether a hand went through something: where the solver missed a contact
there is none to read. So the runner looks for itself, after every frame, with scene queries that change
nothing:

- **`max_penetration_cm`**: the deepest the hand's sphere was found inside a body of the character or the
  static world, as the distance it would have to move to be clear (with `deepest_into`).
- **`tunnel_frames`**: frames in which the straight line from where the hand's centre was a frame ago to
  where it is now crosses a surface, or the centre lies inside a shape. The centre of a sphere cannot get
  closer to a surface than its radius, so either means the hand went through. `first_tunnel_t` and
  `first_tunnelled` say when and what.

Both are sampled once per frame, 90 times a second: what happens between two substeps is not seen. The line
is traced against where the bodies are after the step, which is exact for the world and close enough for
the character (a body would have to move a hand's radius toward the hand's path within one frame to give a
false count).

A run with hands always checks `effectors_not_tunnelled` (no such frame). The metric was shown to fail when
it should: with continuous collision detection switched off (`-CVars "p.Chaos.Solver.UseCCD=0"`) a hand at
25 m/s ends 6.5 cm inside the chest and the check fails; with it on, the same hand stops at the surface.
See `docs/testing.md`.

## Strikes

The `strike_*` scenarios are a hand's target sent in a straight line through a place on the standing
character and back: a slap at 6 m/s and a punch at 8 m/s on the head, the chest and a thigh, and a punch on
a forearm, the thinnest body there is. The speed is in the keys (distance over time); the hand arrives a
little faster than its target, because it has to catch up with it (6.25 and 8.87 m/s). Each one expects no
tunnelling, penetration under 1 cm, no joint faster than 30 rad/s, the arrival speed, a flinch that ends
standing, and that the struck body moved: 5 cm for the head, 3 cm for the chest, 1 cm for a thigh.

To see one, take pictures at the moment of the hit, with the overlays:
`Tools\scenario.ps1 -Name strike_punch_torso -Shot -ShotAt "3.07,3.09,3.25" -Overlay all`.

Two things to know when reading the forces:

- **The peak contact force is not held by the force cap.** The cap limits the spring. A moving hand that is
  stopped also gives up its momentum: at 8 m/s a 1.5 kg hand delivers 12 N*s in one frame, which reads as
  more than 1000 N. To check the cap, hold the hand against something and read `final_contact_force_n`
  (`effector_force_cap` does: 300.0 N against the floor).
- **Contact with the floor counts for the hand, not for the character.** The character's own count leaves
  out the static world and its own bodies; a hand counts everything it touches.

A hand further than `effectors.hand.snap_distance_cm` from its target is put on the target. That is not
physics, so a run in which it happened fails the check `effectors_not_snapped`.

The hands' transforms go into the `bodies` hash of a run that has hands. A run without them hashes exactly
what it did before hands existed.

Expectations. A name that is not in this table is a parse error, so a misspelt one cannot pass unnoticed.

| Expectation | Passes when |
| --- | --- |
| `measures` | Every figure named by its path in `metrics.json` is within its bounds: `{ "control.fall.head_impact_speed_mps": { "max": 2.0 }, "control.fall.hands_first": { "equals": true }, "control.reactions.0.step_lift_s": { "equals": null } }`. A number in a path is an index into a list; `equals` takes `true`, `false`, a text or `null`. A path that is not there fails |
| `rest_within_s` | The body is at rest by that time and stays at rest |
| `max_joint_stretch_cm` | No rigid joint's anchors separate by more, measured from the sixth frame on. A body with `"simulate": false` has no stretch (`metrics.max_joint_stretch_cm` is null) and fails this expectation |
| `max_pelvis_drift_cm` | The pelvis (the root body) never gets further from where it was spawned, over the whole run |
| `min_pelvis_drift_cm` | ...and does get at least this far from it at some time: for a scenario that must show the body was moved (`tune_live_surface`: a body slides away once its friction is taken) |
| `min_head_height_cm` | The head body's centre of mass never drops below this height above the spawn point. Active control only |
| `max_settled_speed_mps`, `max_settled_angular_speed_rads` | No body moves or turns faster after `settle_s`: the measure of jitter at rest. A standing body's idle moves its hands at up to 0.08 m/s, so the speed limit of a standing scenario is 0.15 |
| `min_settled_head_travel_cm` | The head travels at least this far after `settle_s` (the diagonal of the box its centre of mass stays in, `metrics.settled_head_travel_cm`): a body that stands is not a statue. 4 cm with the idle, 0.5 without. Every body's own figure is `body.bodies[].settled_travel_cm` |
| `min_settled_angular_speed_rads` | Some body does turn at least this fast after `settle_s`. For scenarios that make the body less steady on purpose and must show that the change took effect |
| `fell` | `true`: the body fell during the run. `false`: it did not. Fallen means the pelvis (the root body) got below half the height above the spawn point that it started at; a body that sits down on the floor has fallen |
| `min_fall_time_s` | The body did not fall before this time (not falling at all also passes; combine with `fell`) |
| `tuning_changes` | The running body took new tuning values exactly this many times. 0 in a scenario that changes nothing shows that watching the tuning files left the run alone |
| `max_joint_limit_violation_deg` | No joint is found further outside its limits, from the sixth frame on. Measured as the solver limits a joint (`FSandboxPoseRig::LimitExcessDeg`); until M3 part 5 it compared angles the solver does not use and overstated joints that swing and twist at once |
| `end_state` | The body's state at the end: `"standing"`, `"flinch"`, `"falling"` or `"lying"`. Active control only |
| `state_sequence` | Every state the body went through, in order, joined by `>`: `"standing>flinch>standing"`. Active control only |
| `max_joint_speed_rads` | No joint's two bodies rotate faster relative to each other, over the whole run |
| `max_physics_step_mean_ms` | The solver's mean wall-clock time per frame (all substeps) stays below it |
| `min_effector_impulse_ns` | The hands together delivered at least this much contact impulse: they touched something |
| `max_effector_impulse_ns` | ...and at most this much. 0 for a hand that must miss |
| `max_effector_force_n` | No hand's net contact force over one frame was larger. Impacts count, not only pushing |
| `min_effector_final_force_n`, `max_effector_final_force_n` | Bounds on the contact force of the hardest-pressing hand in the run's last frame: the force cap, for a hand held against something |
| `max_effector_penetration_cm` | No hand's sphere was found deeper inside a body of the character or the static world, after any frame |
| `min_effector_impact_speed_mps` | Every hand that touched something was moving at least this fast when it first did: a strike arrived as a strike. Fails if no hand touched anything |
| `min_struck_displacement_cm` | The body of the character each hand touched first moved at least this far from where it was just before. Fails if no hand touched the character |
| `min_com_margin_cm`, `min_capture_margin_cm` | The centre of mass, or the capture point, never got closer to the edge of the support polygon while the body was upright (after `settle_s`), and some foot stood in every such frame. Negative values allow it to be outside by that much. Active control only |
| `max_energy_gain_j` | The body's energy (kinetic plus gravitational) never rose by more above its lowest value since the last input. See "Balance, energy and penetration" |
| `max_rest_after_fall_s` | The body went into `falling` and was at rest, for good, within this time of it. Fails if it never fell. Active control only |
| `max_body_penetration_cm` | No shape of the body was found deeper inside the world (floor, walls, props), from the sixth frame on |
| `max_self_penetration_cm` | No two bodies of the character that may collide were found deeper inside each other, from the sixth frame on |
| `max_pose_error_mean_deg` | While the body stood (not flinching), after the settle time: the angle between each driven muscle's target and its body, averaged over the muscles and then over the frames, stayed at most this (`control.pose.error_mean_deg`) |
| `max_foot_target_error_cm` | At the end of the run, every foot placed by a `foot_target` event is at most this far from its point |
| `min_pose_offset_deg` | In the last frame some muscle's target was turned at least this far from the reference pose (`control.pose.target_offset_max_deg`): the pose of the tuning reached the solver |
| `max_assist_force_share_mean` | The balance assist's commanded force over body weight, averaged over the settled frames, stayed at most this (`control.assist_load.force_share_mean`). The standing scenarios hold it to 0.0093, the owner's M3 bound. Active control only |
| `max_steps` | At most this many steps while upright (`control.step_count`). Active control only |
| `min_steps` | At least this many: the body was made to step. Active control only |
| `effector_strikes` | The hands landed exactly this many blows in all (`effectors.list.<hand>.strikes`): touches at a speed that gives the hand its blow's strength (`effectors.hand.strike_*`) |
| `double_overs` | The body doubled over exactly this many times (`control.double_overs`; `reactions.double_over_*`). Active control only |
| `knockdowns` | A hit on the head knocked the body down exactly this many times (`control.knockdowns`; `reactions.knockdown_head_ns`). Active control only |
| `max_flinches` | The body went into Flinch at most this many times (a held push: once, as the hand lands). Active control only |
| `min_get_ups` | The body got up at least this many times (`control.get_up.successes`). Active control only, with `"get_up": true` |
| `max_get_ups` | The body got up at most this many times: 0 for a body that hands are to keep down for the whole run |
| `max_get_up_attempts` | The body began to get up at most this many times (`control.get_up.attempts`): 1 for a get-up that a push must not end |
| `min_get_up_attempts` | The body began to get up at least this many times: 2 for an attempt that has to be given up and tried again |
| `min_pelvis_anchor_shift_cm` | The pelvis's assist anchor ended at least this far, horizontally, from where it started: it followed the feet (`control.assist_targets`). Active control only |
| `min_lean_cm` | The body leaned into a held push at least this far at some time (`control.balance.lean_max_cm`). Active control only |

A physics asset can be authored with joint anchors that do not coincide in the reference pose; the solver
closes that gap in the first frames. It is reported as `metrics.initial_joint_offset_cm` and is not part of
the stretch metric. In a body that is not simulated nothing closes it and nothing can stretch, so such a
body has that offset and no stretch figure at all (the stock MetaHuman asset: 3.6 cm, in the portrait
scenarios).

Five checks always run: no NaN, the body stays within 50 m of the origin, every frame used the fixed
timestep, the solver's determinism mode is on (`deterministic_solver`: `p.Chaos.Solver.Deterministic`, set
in `Config/DefaultEngine.ini`; without it a run on a busy machine can differ from the one before it), and
every event found its body. With active control two more: that every joint of the physics
asset got a muscle, and that no two bodies that may collide overlap in the spawn pose
(`spawn_pose_clear_of_itself`: they would push each other apart from the first step, and the pose could not
be held). With hands two more: that none of them was snapped to its target, and that none went through
anything (`effectors_not_tunnelled`).

## The whole set

`Tools\suite.ps1` runs every scenario of this directory, eight at a time (`-Parallel`), in about 100 s. The
runs do not depend on each other and each has its own fixed timestep, so their results are the same as
when run one after another; only the wall-clock figures differ. It holds the whole set to two limits on top
of each scenario's own expectations, and prints the worst scenario of each: no simulated body's joints
stretch by more than 2 cm, and no scenario's physics step takes more than 1.5 ms on average.

The physics step is timed by the clock, and a run that shares the processor with seven others reads about
twice as long as it is. A scenario that is over a step limit after the parallel pass is therefore run again
by itself, and that run counts. Sharing never makes a step read shorter, so nothing over the limit slips
through; for figures to write down, use `-Parallel 1` (6.5 minutes).

`Tools\baseline.ps1 -Save <name>` keeps the results of the latest suite run under `Saved\Baselines\<name>`,
and `-Compare <name>` says which scenarios have other hashes now and what else moved in them (states,
drift, stretch, assist load, margins, energy, penetration). `suite.ps1 -SaveBaseline <name>` and
`-Baseline <name>` do both in one go. A baseline is taken before a change to the body or its controller;
the scenarios the change should not touch must come out with the same two hashes.

`Tools\sweep.ps1` runs chosen scenarios over a set of tuning values and puts chosen metrics in one table:

```
Tools\sweep.ps1 -Name iterations -Scenarios stand_npc -Vary "solver.position_iterations=4,8,16,32,64" `
    -Metrics "metrics.max_pelvis_drift_cm,metrics.max_settled_angular_speed_rads,control.assist_load.force_share_mean"
```

`-Vary "a=1,2;b=3,4"` runs every combination, with `-OneAtATime` each value alone; `-Files <dir>` adds
generated scenario files; the columns are paths into `metrics.json`. The values are passed to each run as
`-Tune`, so `Config/Tuning` is not written to. The table is printed and saved as
`Saved\Sweeps\<name>\sweep.csv` and `sweep.json`; every run keeps its own `metrics.json` and `series.csv`.

## Balance, energy and penetration

These are measured in every run with a simulated body, after every frame, by reading the bodies and asking
the scene. Nothing is changed by it: a run gives the same hashes as before they were measured.

**Balance** (`control.balance`, active control only) is what the body itself senses
(`USandboxBodyController::MeasureBalance`): a foot stands when the floor pushes up on it with at least
`balance.foot_contact_min_n`; the support polygon is the hull of the soles of the standing feet; the
capture point lies ahead of the centre of mass by its horizontal velocity times sqrt(height / gravity), and
is where the body would have to be supported to come to rest. While it is inside the support polygon the
body can stop where it stands. The figures cover the frames after `settle_s` in which the body is upright
(standing or flinching): the lowest and the mean margin of the centre of mass and of the capture point to
the polygon's edge, how many frames the capture point was outside, how many feet stood, and
`support_share_mean`, the floor's push on the two feet over the body's weight (1 when the feet carry it).

**Energy** (`energy`) is kinetic (translation and rotation of every body) plus gravitational above the
floor. Falling turns one into the other, so kinetic energy alone says nothing; their sum can only go down
while nothing feeds the body. `max_gain_j` is the largest rise of the sum above its lowest value since the
last input, where an input is a scripted impulse, a pin, a console command, a tuning change or a hand
touching something. A body under control is watched only while it is `falling` or `lying`: upright, its
muscles and the assist hold it up and that is no fault. What the figure catches is a body thrown back up by
the solver or by its own muscles: a passive NPC dropped from 1.5 m lands with a foot 8.4 cm inside the
floor, and being pushed out again gives it 171 J (`drop_npc`); an 80 kg body lifted by 1 cm is 7.8 J.

**The fall** (`control.fall`): when the controller let the body go (`started_t`), how long after that it
was at rest for good (`rest_after_s`), and how far and which way the centre of mass went over the floor
(`distance_cm`, `direction_deg`: 0 is +x, 90 is +y). At rest means that the body as a whole moves no faster
than 0.05 m/s and turns no faster than 0.2 rad/s (root mean square over its bodies, weighted by mass).
These are the runner's own figures and do not follow the tuning's rest thresholds.

**Penetration** (`penetration`): how deep a shape of the body is inside the world (`max_world_cm`, with the
bone, what it is inside and when), and how deep two bodies of the character are inside each other
(`max_self_cm`), each as the distance the shape would have to move to be clear. The player's hands are
left out here: they are measured on their own. Bodies of the character count against each other only where
the physics asset lets them collide; `self_collision_pairs` says how many such pairs there are (127 for the NPC), because
with 0 the body passes through itself and the figure reads 0 whatever happens. The first five frames are
reported apart (`spawn_world_cm`, `spawn_self_cm`). Boxes, capsules and spheres are measured;
`unmeasured_shapes` counts the others.

`series.csv` has all of this for every frame: time, state, feet standing, the floor's force on each foot,
kinetic and gravitational energy, the body's speed as a whole, both margins, both penetrations, the
assist's force share, the fastest joint of that frame with its speed, and last `decision_margin_cm`, the margin
the controller decides on (the capture point against the whole sole of every foot the floor touches; the two
margins before it are against the part of the soles that touches). The joint speed is the relative
angular velocity of the two bodies at the end of the frame: a peak in one frame that is gone in the next is
a snap of one substep, not a body that is flying apart.

**What the body does about its balance** (M3 part 3; `control.balance` and `control`):

- `step_needed_frames`, `step_needed_first_t`: frames in which the controller found that the capture point
  was further outside the support than the ankles and hips can bring back (`balance.recoverable_cm`) but
  within a step's reach. Until the recovery step exists the assist holds such a body; the push matrix calls
  that outcome `held`. `decision_margin_min_cm` is the lowest margin the controller decided on: it judges
  against every foot the floor touches (or touched within `balance.lift_grace_s`), not only the feet that
  stand by `balance.foot_contact_min_n`, and moves the capture point by the held push (below).
- `replants`: times a foot was planted anew where it landed. `control.assist_targets` has every assist
  anchor's final place (relative to the spawn point) and how far it moved (`shift_cm`): the anchors of
  pelvis, chest and head follow the feet's (`stand_foot_moved`).
- `ankle_lean_max_deg`, `hip_bend_max_deg`, `ankle_integral_forward_cm`, `ankle_integral_side_cm`: the
  strategies' largest turns and the slow part's shift of the centre of pressure at the end.
- `sustained_force_max_n`, `lean_max_cm`: the largest force felt in every frame of `reactions.pressure_s`
  (a held push; a blow leaves nothing here) and the largest lean into it.
- `control.steps`, `step_count`, `foot_lifts`: measured by the runner from the feet the floor touches. A
  step is a foot that leaves the floor for at least 0.1 s and lands at least 5 cm from where it left; a
  foot knocked aside along the floor, or lifted and put back, is not one.
- `control.assist_load.by_body`: the assist's force share by the body it holds (pelvis, chest, head, feet),
  horizontal and vertical parts. In quiet standing before part 3 the pelvis's horizontal pull was 0.0135
  of the 0.0171.

**Target pose** (`control.pose`): how far the muscles' targets are turned from the reference pose in the
last frame (`target_offset_max_deg` and its joint; 0 for the standing pose of zeros), and how well the
muscles hold them while the body stands: the mean over driven muscles of target-to-body angle per frame,
averaged (`error_mean_deg`, 1.08 standing) and at its largest (`error_mean_max_deg`), and the single worst
muscle (`error_worst_deg`, `error_worst_joint`; a collarbone under the arm's weight, about 5 degrees).

**Get-ups** (`control.get_up`, in a run with `"get_up": true`): how many times the body began to get up and how many
ended standing (`attempts`, `successes`); the last attempt's clip, when it began and how long it took to stand
(`started_s`, `duration_s`, -1 if it did not), how long its clip waited for the body in all (`stall_s`), and why the
last attempt that was given up was given up (`given_up`); hits felt while getting up (`hits`) and the most the body
was pressed with (`pressed_max_n`: the force felt in every frame of `reactions.held_push_s`, which is what
`getup.hold_force_n` is compared with); and what the assist did over all the time spent getting up (`seconds`): its
commanded force over body weight, mean and largest (`assist_force_share_mean` 0.25 to 0.28 since M3 part 7, 0.44 to
0.49 before), and its largest torque share. The
run's log has a line four times a second while the body gets up: the clip's time, how far the pelvis and the feet
are from where the clip has them, and what the floor carries of each foot.

**Clips** (`control.clips`, one entry for each clip a `clip_pose` event read): its length and keys, where it
has the pelvis at its start and end (`pelvis_start_cm`, `pelvis_end_cm`: in the space of the clip's root,
scaled to this body), and for every joint how the clip fits the body. `reference_turn_deg` is the angle between
the bone's orientation in the reference pose of the clip's skeleton and of the body's mesh, and
`reference_direction_deg` the angle between the ways the two hold that segment of an arm or a leg (10 to 18 and
3 to 13 degrees for the sample's mannequin against the MetaHuman body: why a clip's rotations are not copied).
`limit_excess_deg` is the furthest the clip asks the joint beyond its range, with the time and the joint's
angles then (`limit_angles_deg`: twist, swing1, swing2, against `limit_ranges_deg`); the muscle is asked for
the end of the range instead. `limit_excess_max_deg` and `limit_excess_joint` name the worst joint of the clip.

`body.bodies` lists every body's mass and its principal moments of inertia (`inertia_kg_cm2`) as the solver
uses them: from the mass and the shapes, times the physics asset's inertia scale.

A strike's outcome can depend on the exact point it lands on. `strike_punch_forearm_heights` strikes the
forearm at four more heights, one every 2 s, so that a scenario cannot pass by landing on the one good spot
(see "Inertia of the hands" in `Specs/README.md`).

The player of a scenario run is a spectator without a body: the project's own pawn, with its two physical
hands, exists only in an interactive session.

## How natural the motion is

Measured in every run of a simulated body since N1 of the natural-body track (`docs/natural-body.md`;
`FNaturalMeasures`), by reading only: a run has the hashes it had. What each figure is checked against is in
`docs/testing.md`, "N1"; the `measure_*` scenarios are those checks, each with an answer worked out beforehand, and a
change that makes one of them fail has broken a measure (or the solver), not the body.

- **How a fall meets the floor** (`control.fall`): `touches` lists every body but the feet the first time the world
  touched it after the body was let go: its `t` (the start of the step), its `speed_mps` and downward `down_mps` at the
  start of that step. `first_touch_bone`, `time_to_first_touch_s`, `time_to_hand_s`, `time_to_trunk_s` (pelvis, spine or
  collarbone), `hands_first` (a hand or a forearm in an earlier step than the trunk; null if neither touched),
  `hands_ahead_first` (the same of a hand or forearm that lay ahead of the chest along the way the body went over the
  floor: each touch's `ahead_cm`, null for a body going slower than 0.2 m/s over it; the rule of C14), `hand_ahead_cm`,
  `head_touched` (also a head that is laid down), `head_struck` (it came down at 0.5 m/s or more, at any of the times it
  met the floor), `head_impact_speed_mps` (the fastest of those), `pelvis_impact_speed_mps`, `hand_impact_speed_mps` (the
  first touch). These are the run's first fall; `falls` has every one.
- **Reaction timing** (`control.reactions`, one entry a hit the body told of): from `hit_t`, the start of the first step
  after the hit was felt (a scripted impulse acts in that step, a contact acted in the one before), the first frame in
  which some muscle's target is more than 1 degree from what it was (`first_target_change_s`, `first_target_joint`),
  some stiffness more than 1% (`first_strength_change_s`), a foot stepped for balance (`step_lift_s`, `step_land_s`), the
  fall began (`fall_start_s`); null for what did not happen within 1.5 s. `target_speed_before_dps` is the fastest any
  target turned in the 0.1 s before: with a body that was already moving its targets the first figure means little.
- **Joint effort** (`control.effort`): each muscle's torque from the spring law, with the state a step ended in and the
  drive it was made with, capped axis by axis as the solver caps it. Per joint `torque_max_nm`, `torque_p95_nm`,
  `torque_final_nm` (mean of the last second), `cap_nm`, `at_cap_frames`, `at_cap_upright_share`, `power_max_w`,
  `work_j`; for the body `at_cap_share` (upright frames in which any muscle is at its cap), `at_cap_joint_share_max` and
  `at_cap_joint`, `positive_work_j`, `power_peak_w`. **An estimate**: the solver moves the bodies after its drives (joint
  projection, unfinished iterations) by 0.2 to 0.35 degrees, so a torque is wrong by about 0.005 rad of its stiffness:
  0.2 N*m at 40 N*m/rad, 3 to 4 N*m at 600 to 1200. `power_peak_w` in an impact is a figure of that kind.
- **The assist by state** (`control.assist_by_state`): what the world pulls the body with in each state, over the whole
  run: force over body weight and torque over weight times height, mean and largest (a blow's first frame is in it).
- **Time beyond a joint's range**: `metrics.limit_excess_share` (joint-frames more than 0.5 degrees outside, of all
  while the body does not lie), the worst joint's share, and each joint's own in `body.joints`.
- **Jumps**: the largest turn of a muscle's target in one frame (`control.pose.target_step_max_deg`), the largest
  relative change of a stiffness (`strength_step_max`), and what the two together change of a muscle's torque on the
  body as it is (`command_step_max_nm`): a target that jumps while its muscle has no strength (a get-up's start, 127
  degrees) asks for nothing, and a target that moves fast (a swinging leg's) reads as a jump of that much. `command_jump_max_nm` is the part
  that is a jump: what is left after the change has been explained by a motion that goes on from the two frames before or
  comes from the two after (`FSandboxMotionStats::CommandJumpNm`), with its joint and time; `target_jump_max_deg` is the same
  of the target alone. A jump over two frames reads as a motion, and the run's last frame is not judged.
  `metrics.joint_speed_step_max_rads` is the largest change of a joint's turning speed.
- **The head** (`metrics.head_accel_max_mps2`, `head_jerk_rms_mps3`): from the velocity of the head body's centre of
  mass, a mean of three frames.
- **Stillness** (`metrics.stillness`): a body is still while no part of it has gone further than 0.5 cm in the last
  second. `longest_still_s` with its time and state, `still_share_by_state`, `longest_still_by_state_s`,
  `travel_by_state_cm` (how far the part that went furthest went in a second: 1.5 to 5.8 cm with the idle, 0.3 to 4
  without it), and `target_still_longest_s`: how long the targets stood still (0.02 degrees a frame) while the body
  neither stood nor lay, with each state's own.
- **Pushes** (`control.pushes`, one entry for each `impulse` with a `duration_s`): `peak_along_cm` (how far the push took
  the part) and `peak_stiffness_n_per_cm`, `against_cm` (how far the body went against it: it leans into a held push),
  `settled_displacement_cm` and `whole_body_cm` (the part and the whole centre of mass over the push's last 0.2 s),
  `stiffness_n_per_cm`, `local_stiffness_n_per_cm`, `return_s`. `Tools\compliance.ps1` is a table of them.
- **Sway** (`metrics.sway`, over the longest unbroken stretch of Standing after the settle time): spread and mean speed of the centre of mass
  and of the centre of pressure, along and across the way the body faces, smoothed over 0.1 s (`cop_speed_raw_cmps` is
  not), `period_s` (null for a motion that does not come round) with its correlation, `floor_share_mean` (the floor's push
  over the weight) and `cop_offset_*_cm` (the centre of pressure's mean less the centre of mass's). The pressure is taken
  from the solver's contact points after every substep.
- **Likeness of the pose** (`metrics.pose_prior`, with `"pose_prior"`): every third frame, the mean angle over the joints
  and the pelvis's lean to the nearest pose of the database, by state. The stamp says which database it was.

`Tools\natural.ps1` runs a fixed set and prints these as a row for each criterion of the track.

## Playing a scenario

`Tools\play.ps1 -Name <scenario>` opens a scenario's scene as an interactive session: its map, its body and
its camera (where the player starts), with the desktop pawn, its hands and its tools. The scenario's events
and expectations are not used. Without `-Name` it opens `room_stand_npc`, the standing character in the
test room. The keys are on the screen and in `Source/SandboxInteraction/Public/SandboxPawn.h`.

The session runs on the scenario's fixed timestep and holds the frame rate to it, so a second of the
session is a second on the clock. Pause, single step and slow motion never change the physics step: the
world either makes its full step or stands still.

`Tools\play.ps1 -Smoke` runs the session without a window or a person: it uses every tool through the
functions the keys call, checks what happened (the overlays draw, or without a renderer do nothing; the
impulse tool makes the character flinch, a paused world does not move, a step is one frame, the drag handle
lifts an arm, a reaching hand is felt, a reset puts the character back) and writes
`Saved\Play\<name>\play.json`. `-Smoke -Shot` renders it and saves the last frame. `Tools\bootstrap.ps1`
runs the self-test.

## Overlays

Debug overlays draw what the character's controller and the hands are doing on top of the picture:

| Layer | Shows |
| --- | --- |
| `com` | The centre of mass, a line down to the floor and a ring where it lands: green inside the support polygon, red outside, grey when no foot stands |
| `support` | The support polygon, yellow: the hull of the soles of the feet that stand, as the body itself senses them |
| `capture` | The capture point as a ring on the floor with a line from under the centre of mass: turquoise inside the support polygon, red outside, grey when no foot stands |
| `joints` | Each bone from its joint (white; orange while a flinch has weakened its muscle, grey without a drive), and where its muscle turns it to: green within 2 degrees, yellow at 8, red from 20 |
| `assist` | Each balance-assist target as a cyan cross, with a line to the body it holds |
| `contacts` | The contacts of the last physics step as arrows along the force on the character or the hand, 1 cm per 20 N: blue against the floor and walls, orange with the force in newtons against a body that moves |
| `state` | The controller's state, its cause and its age above the head, and a block of figures at the bottom left: assist load, centre of mass and capture point against the support polygon with the floor's force on each foot, fastest joint, the hands |
| `hands` | Each hand's target as a ring and the spring between target and hand: green, yellow from half the force cap, red at the cap. The drag handle in magenta |

They are switched by the console command `Sandbox.Overlay`: `all`, `off`, `toggle`, layers by name
(`Sandbox.Overlay com,support,capture`), or a layer taken away (`Sandbox.Overlay -joints`). A session has
the V key for `toggle`; `Tools\play.ps1 -Overlay all` and `Tools\scenario.ps1 -Shot -Overlay all` start
with them on, and a scenario can switch them itself with a `console` event. With `-ShotAt "3.07,3.25"` a
rendered scenario run also saves a picture at those simulated times (`shot_3.070.png`), since what a strike
does is over long before the last frame.

The overlay only looks: it reads the bodies and listens to their contact events, and writes nothing back. A
run gives the same hashes with it and without it. Without a renderer there is nothing to look at, so it
stays off whatever is asked: it does not tick and listens to nothing. In a headset it draws its lines and no
text.

## Tuning overrides

The body's tuning comes from the files in `Config/Tuning`; names, units and the ways to change a value
are in `Config/Tuning/README.md`.

`Tools\scenario.ps1 -Name stand_npc -Tune "muscles.legs.stiffness_nm_per_rad=400,solver.position_iterations=32"`
overrides values for one run, and `-TuningDir <dir>` reads the files from another directory. An unknown
name, a value out of range or a tuning file that cannot be used stops the run with the reason
(`SCENARIO ERROR`, exit code 2).

The run keeps watching the tuning files, every 30 frames, and takes over an edit made while it runs. With
files that do not change this has no effect on the result.

## Steps and falls

A body that loses its balance staggers and steps (M3 part 4); it never decides to fall. Its capture point
further out than the ankles and hips bring back makes it step, beyond one step's reach it steps at once and
goes on stepping, and it falls when its posture gives way: the pelvis tilted by `thresholds.fall_tilt_deg` or
sunk by `fall_pelvis_drop_cm`. As it falls it takes the pose of its fall (`poses.fall_back`, `fall_forward`,
`fall_side`: it sits down and curls up, or puts its hands out, or reaches for the floor with one arm), and once
it is down and nearly still it lies and lets its muscles go.

- A `step` event asks a standing body to put a foot (`body`) `offset_cm` from where it stands: the weight goes
  onto the other foot first, then the foot swings (`step_forward`, `step_side`, `step_back` and their mirror
  images).
- `control.recovery_steps` lists every step the controller made, asked for or its own: when it started and
  landed, how long it was planned and came out, the capture point it was planned on, `planned_margin_cm`,
  whether it was cut to the reach or kept to its side, whether it crossed or passed the other foot, and
  `regroup` for a step that only brought the feet back to their stance (`control.regroup_step_count`).
  `control.steps` is the same seen from outside: every foot that left the floor and stood again elsewhere.
- After recovery steps the feet stand wherever those put them, a lunge or crossed legs. A body that has stood
  still for `balance.regroup_wait_s` (0.6 s) brings a foot back beside the other: the weight goes onto the
  other foot, the body rises on that leg, the foot comes over. `control.stance_error_cm` is how far the feet
  stand from their stance at the end of the run (1 to 5 cm after a regrouping step, 25 to 60 cm without one).
  A step that was asked for (`step`) is not followed by one: the feet stay where they were asked to be.
- A staggering body that comes to rest with its centre of mass off its support steps once more
  (`balance.rest_step_s`, 0.3 s). The allowance of `balance.recoverable_cm` is for a body in motion; at rest
  nothing brings the centre of mass back, and the stagger's upright hold on the pelvis kept such a body up for
  good: on crossed legs after two steps across, one foot before the other, 2.4 cm outside its whole soles (no
  step) and 6 cm outside the edges it stood on (never "over its support", so never Standing).
  `stagger_rest_off_support` holds it (soles made grippy in the run, which is what brings the stance about; with
  `-Tune "balance.rest_step_s=0"` it ends in Stagger). The run's log says when the rule asked for a step.
- Expectations: `max_recovery_steps` (steps for balance and asked for; regrouping steps are not counted),
  `max_landing_error_cm` (every step landed this near its target, and there was at least one) and
  `max_stance_error_cm` (the feet are back in their stance at the end).
- The muscles of a falling body do work: folding the legs and putting the arms out costs 10 to 30 J, which
  `max_energy_gain_j` counts. The falling scenarios allow 40 J; a standing or stepping body is still held to 1 J.

The scenarios: `push_chest_80_steps` and `push_pelvis_80_side_steps` (a step forward, steps across),
`stagger_rest_off_support` (crossed legs after steps across: a third step out of them),
`effector_push_chest_steps` (a hand pushes the chest 70 cm back: the body backs away and stands; it used to be
`effector_push_chest_falls`), `push_thigh_200_stumbles` (200 N*s on a thigh is run off in four steps; it used to
be `push_thigh_200_falls`), and three falls after 200 N*s on the chest: `hit_chest_200_falls` (backward),
`hit_chest_200_forward_falls` and `hit_chest_200_side_falls`.

To look at a motion rather than at its numbers, `Tools\film.ps1 -Name hit_chest_200_falls -Times
"3.1,3.2,3.3,3.4,3.5,3.7,4.0,5.0"` renders the run with a picture at each of those times and joins them into
`Saved\Films\<name>\sheet.png` (`-Camera side|front|back|quarter` or a location and a point to look at,
`-Events <file>` for other events, `-Tune`, `-Overlay`). A body that passes every check can still move like a
statue: the arms held out, a fall like a plank and a body frozen in a lunge all passed the suite.
## Push matrix

The stand of falls and get-ups, `Tools\getup-stand.ps1`, generates its scenarios the same way: 50 falls drawn
from a seed (26 on the bare floor, 8 each near a wall, the table and the chair of `Specs\rooms\getup_room.json`),
each with `"get_up": true` and 40 s to end standing in (a trial ends as soon as its body has stood for 3 s with
nothing left to happen to it, `end_when_standing_s`; `-StopStandingS 0` runs each to the end); a blow that did not
fell the body is drawn again. `Tools\getup-seeds.ps1` runs the stand on three seeds side by side. The table
and the chair stand together in the middle of the room; the script reads where from the spec, puts no body where
its arms, out as it appears, would touch a piece or a wall, and has the falls at a wall in the room's corner
quarters, reporting how near to the furniture each came. It prints one row a fall and the share that stand
(`-Film failed` for a sheet of pictures of each that does not, `-Film "36,42"` of those trials). With `-PressN 300`
every fallen body is also pressed down on the pelvis with that force from 5 to 25 s, and the stand counts the
bodies that stood while they were pressed: what a hand does to a get-up, asked of 50 falls and not of one (under
300 N two of three get up, under 600 N none: `getup.hold_force_n`).

To look at what a recorded clip asks of the body, `Tools\clip-film.ps1 -Clip M_ragdoll_getup_stand_B -ClipTimes
"0,0.4,0.8,1.2,1.6,2.0,2.5" -Carry -RaiseCm 60`: the body hangs by its welded pelvis, is asked for the clip's pose
at each time in turn, and a picture is taken of each (`-Carry` turns and moves the pelvis as the clip does;
without it the pelvis stays upright).

`Tools\push-matrix.ps1` pushes the standing NPC with every combination of impulse (10, 30, 80, 200 N*s),
body (head, chest, pelvis, both thighs) and direction (8), 160 runs, eight engine processes that take the
runs from one queue, in about 50 seconds.
It generates one scenario per push under `Saved\Matrix\push\scenarios`, runs them with
`Tools\scenario.ps1 -File`, names the outcome of each push and writes `Saved\Matrix\push\matrix.json`.

An outcome is `stood` (no step, never needed one), `held` (no step although one was needed: the assist held
the body), `stepped` (one or more steps, still up) or `fell`. A step is a foot that left the floor and stood
again 10 cm or more away (`-StepMinCm`; owner's decision of 24 November 2026), less the steps that only brought
the feet back together (`control.recovery_steps[].regroup`); a step the controller itself made for balance counts
whatever its length. A foot that hops less far is a shuffle: each run's `shuffles`, summed for every row, printed
and judged by nothing. (The run's own record, `control.steps`, starts at 5 cm; counted from there, one recovery
step read as two wherever the other foot hopped 5 to 6 cm as the first landed: `-StepMinCm 5` gives that count
back.) `matrix.json` has `step_min_cm`. The outcomes are judged by the owner's table of M3:
10 N*s stood; 30 N*s never a fall, forward and sideways stood, backward (225, 270, 315 degrees) and any push
on the head stood or one step; 80 N*s on the trunk (chest, pelvis) stepped, backward stepped or fell, on the
head and the thighs stepped or fell, and at 80 N*s anywhere stood if no step was ever needed (owner's decision
in part 4: standing is a failure only when the assist held a body that needed a step); 200 N*s on the chest
and the head fell, on the pelvis and the thighs fell or stepped (a push near the centre of mass does not turn
the body over, and it may run the push off: decided in the audit of November 2026, since the body always tries
to step). `held` is a failure everywhere. Mirror-image pushes that end differently are listed and counted,
not judged. The table prints `s`, `h`, `S<n>`, `F`, with
`!` for a failure. `-Enforce` names the impulses whose rows are judged (all four by default), and
`-Rules m2` checks only the M2 gate (the smallest impulse never fells, the largest always does). By default
each impulse is a single blow; `-PushSeconds 0.1` delivers it as a push. `-Tune` passes tuning overrides
to every run. `Tools\baseline.ps1 -CompareMatrix <baseline>` names the pushes that end otherwise than in a
baseline's matrix.

## Output

Rendered runs add `performance` (GPU, render-thread and game-thread time per frame and the frame interval:
mean, median, 99th percentile, maximum, after a 90-frame warm-up) and `xr` (whether the run was in stereo,
the runtime, and the headset's render target size). In a headset the game-thread figure includes waiting
for the compositor, so use `gpu_ms` against the frame budget.

`metrics.json` has `pass`, the list of `checks` with value and limit, and these blocks:

- `metrics`: peak speeds, stretch, time to rest, drift of the pelvis and of the centre of mass, lowest head
  height, peak speeds after `settle_s`, the fastest joint, how far a joint got outside its limits
  (`max_joint_limit_violation_deg`, with the joint, its three angles, their half-ranges and the time in
  `worst_limit_joint`, `worst_limit_angles_deg`, `worst_limit_ranges_deg` and `worst_limit_t`; measured as the engine's solver limits a joint, the twist and each swing against its own range), and whether and when the body fell (`fell`,
  `fall_time_s`, with the pelvis height that counts as fallen in `fall_pelvis_height_cm`).
- `body`: every body's mass and its peak speeds after `settle_s` (which body jitters), and every joint's
  parent and limits as instanced. Each joint also reports where the spawn pose sits against those limits
  (`spawn_twist_deg`, `spawn_swing1_deg`, `spawn_swing2_deg`) and the gap between its two anchors in the
  spawn pose (`spawn_anchor_offset_cm`).
- `control`: `passive` or `active`; for active runs the number of muscles and assist controls, the tuning
  and `assist_load`. `tuning` holds every value the body had at the end of the run, `tuning_source` the
  directory and files they were read from and the overrides in force, and `tuning_changes` one entry for
  each time the running body took new values: the frame and simulated time of the first physics step that
  used them, each changed value as `from` and `to`, and the pelvis height at that moment. A run that
  changes nothing has an empty list. `states` is the state timeline: each state with the simulated time and
  frame of the first physics step made in it and what caused it; `end_state` and `state_sequence` are the
  same in short. A new state brings its own tuning values, which is not counted as a tuning change.
  `balance` and `fall` are described under "Balance, energy and penetration".
  The load is the balance assist's commanded force as a fraction of body
  weight (`force_share_*`) and its commanded torque as a fraction of weight times centre-of-mass height
  (`torque_share_*`), mean and maximum after `settle_s`. It is computed from the assist springs' settings
  and the bodies' distance from their targets, because the solver does not report drive forces.
- `energy` and `penetration`: see "Balance, energy and penetration".
- `physics_step_ms`: wall-clock time of the solver per frame, all substeps together (mean, median, 99th
  percentile, maximum), and `solver_steps_per_frame`, which must read 2.
- `timing`, and two `hashes`: a CRC of all body transforms over every frame and one of all bone transforms.
  Equal hashes mean two runs were identical in every frame.

A console variable given with `-CVars` that the engine does not know is accepted in silence and changes
nothing, which reads like proof that it makes no difference. `Tools\scenario.ps1` prints
`SCENARIO WARNING` for such a name.

`trace.json` holds the transforms at each checkpoint for measuring how far two runs drifted apart
(`Tools\compare-runs.ps1`). Its first checkpoint (`t` = 0, `frame` = -1) is the spawn pose before any physics
step, so every later one can be read as a displacement from it.
