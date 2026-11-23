# Scenarios

Each `*.json` file here is one reproducible physics run. `Tools\scenario.ps1 -Name <file name without .json>`
runs it at a fixed timestep with no renderer and writes `Saved\Scenarios\<name>\metrics.json` and `trace.json`.

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
| `body.mesh` | yes | Skeletal mesh object path |
| `body.physics_asset` | no | Overrides the mesh's own physics asset |
| `body.location_cm` | no, default `[0, 0, 150]` | Spawn location of the mesh origin (the feet). The floor's top is at z = 0 |
| `body.rotation_deg` | no | `[pitch, yaw, roll]` |
| `body.attached_meshes` | no | Extra skeletal meshes that follow the body's pose and have no collision (a MetaHuman face) |
| `body.simulate` | no, default `true` | `false` keeps the body kinematic in its reference pose, for portraits and rendering-cost scenes |
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
- In the scenario's own space the character faces +y and its left is +x, when it is spawned unrotated.

`effectors` in `metrics.json` has, for each hand: `contact_impulse_ns` (the sum of all contact impulses),
`touched_impulse_ns` (the same by what was touched: a bone of the character, or an actor's name),
`first_contact_t`, `contact_frames`, `max_contact_force_n` (the largest net contact force over one frame),
`final_contact_force_n` (the same in the run's last frame), `max_gap_cm` and `final_gap_cm` (distance from
the hand to its target), `max_speed_mps`, `max_demand_n` (the largest force the spring law asked for before
its cap) and `snaps`. What the character felt is in `control`: `contact_impulse_ns` and `contact_reports`.

A strike adds, per hand: `first_touched` and `first_contact_speed_mps` (what it touched first and how fast it
was moving at the start of the physics step in which it did), `struck_bone` and `struck_displacement_cm`
(the body of the character it touched first, and the furthest that body's centre of mass got from where it
was just before the contact), and what the next section measures.

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
| `rest_within_s` | The body is at rest by that time and stays at rest |
| `max_joint_stretch_cm` | No rigid joint's anchors separate by more, measured from the sixth frame on. A body with `"simulate": false` has no stretch (`metrics.max_joint_stretch_cm` is null) and fails this expectation |
| `max_pelvis_drift_cm` | The pelvis (the root body) never gets further from where it was spawned, over the whole run |
| `min_head_height_cm` | The head body's centre of mass never drops below this height above the spawn point. Active control only |
| `max_settled_speed_mps`, `max_settled_angular_speed_rads` | No body moves or turns faster after `settle_s`: the measure of jitter at rest |
| `min_settled_angular_speed_rads` | Some body does turn at least this fast after `settle_s`. For scenarios that make the body less steady on purpose and must show that the change took effect |
| `fell` | `true`: the body fell during the run. `false`: it did not. Fallen means the pelvis (the root body) got below half the height above the spawn point that it started at; a body that sits down on the floor has fallen |
| `min_fall_time_s` | The body did not fall before this time (not falling at all also passes; combine with `fell`) |
| `tuning_changes` | The running body took new tuning values exactly this many times. 0 in a scenario that changes nothing shows that watching the tuning files left the run alone |
| `max_joint_limit_violation_deg` | No joint is found further outside its limits, from the sixth frame on |
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

A physics asset can be authored with joint anchors that do not coincide in the reference pose; the solver
closes that gap in the first frames. It is reported as `metrics.initial_joint_offset_cm` and is not part of
the stretch metric. In a body that is not simulated nothing closes it and nothing can stretch, so such a
body has that offset and no stretch figure at all (the stock MetaHuman asset: 3.6 cm, in the portrait
scenarios).

Four checks always run: no NaN, the body stays within 50 m of the origin, every frame used the fixed
timestep, and every event found its body. With active control a fifth one checks that every joint of the
physics asset got a muscle, and with hands two more: that none of them was snapped to its target, and that
none went through anything (`effectors_not_tunnelled`).

`Tools\suite.ps1` holds the whole set to two limits on top of each scenario's own expectations, and prints
the worst scenario of each: no simulated body's joints stretch by more than 2 cm, and no scenario's physics
step takes more than 1.5 ms on average.

The player of a scenario run is a spectator without a body: the project's own pawn, with its two physical
hands, exists only in an interactive session.

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
| `support` | The support polygon, yellow: the hull of the soles of the feet the floor pushed on in the last step |
| `joints` | Each bone from its joint (white; orange while a flinch has weakened its muscle, grey without a drive), and where its muscle turns it to: green within 2 degrees, yellow at 8, red from 20 |
| `assist` | Each balance-assist target as a cyan cross, with a line to the body it holds |
| `contacts` | The contacts of the last physics step as arrows along the force on the character or the hand, 1 cm per 20 N: blue against the floor and walls, orange with the force in newtons against a body that moves |
| `state` | The controller's state, its cause and its age above the head, and a block of figures at the bottom left: assist load, centre of mass against the support polygon, fastest joint, the hands |
| `hands` | Each hand's target as a ring and the spring between target and hand: green, yellow from half the force cap, red at the cap. The drag handle in magenta |

They are switched by the console command `Sandbox.Overlay`: `all`, `off`, `toggle`, layers by name
(`Sandbox.Overlay com,support,joints`), or a layer taken away (`Sandbox.Overlay -joints`). A session has
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

## Push matrix

`Tools\push-matrix.ps1` pushes the standing NPC with every combination of impulse (10, 30, 80, 200 N*s),
body (head, chest, both thighs) and direction (8), 128 runs, several at a time, in about 3 minutes. It
generates one scenario per push under `Saved\Matrix\push\scenarios`, runs them with
`Tools\scenario.ps1 -File`, prints a table of who fell and writes `Saved\Matrix\push\matrix.json`.
The rules it checks are the M2 gate: the smallest impulse never causes a fall, the largest always does,
mirror-image pushes end the same way, and no run stretches a joint by more than 2 cm. By default each
impulse is a single blow; `-PushSeconds 0.1` delivers it as a push. `-Tune` passes tuning overrides to
every run, which is how a threshold is tried before it goes into the file.

## Output

Rendered runs add `performance` (GPU, render-thread and game-thread time per frame and the frame interval:
mean, median, 99th percentile, maximum, after a 90-frame warm-up) and `xr` (whether the run was in stereo,
the runtime, and the headset's render target size). In a headset the game-thread figure includes waiting
for the compositor, so use `gpu_ms` against the frame budget.

`metrics.json` has `pass`, the list of `checks` with value and limit, and these blocks:

- `metrics`: peak speeds, stretch, time to rest, drift of the pelvis and of the centre of mass, lowest head
  height, peak speeds after `settle_s`, the fastest joint, how far a joint got outside its limits
  (`max_joint_limit_violation_deg`; measured axis by axis, while the engine limits the two swings together,
  so it can read 0 for a joint slightly outside), and whether and when the body fell (`fell`,
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
  The load is the balance assist's commanded force as a fraction of body
  weight (`force_share_*`) and its commanded torque as a fraction of weight times centre-of-mass height
  (`torque_share_*`), mean and maximum after `settle_s`. It is computed from the assist springs' settings
  and the bodies' distance from their targets, because the solver does not report drive forces.
- `physics_step_ms`: wall-clock time of the solver per frame, all substeps together (mean, median, 99th
  percentile, maximum), and `solver_steps_per_frame`, which must read 2.
- `timing`, and two `hashes`: a CRC of all body transforms over every frame and one of all bone transforms.
  Equal hashes mean two runs were identical in every frame.

`trace.json` holds the transforms at each checkpoint for measuring how far two runs drifted apart
(`Tools\compare-runs.ps1`). Its first checkpoint (`t` = 0, `frame` = -1) is the spawn pose before any physics
step, so every later one can be read as a displacement from it.
