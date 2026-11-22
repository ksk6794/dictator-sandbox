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
are three: `body.json` (the character), `effectors.json` (the player's hands) and `tools.json` (the
desktop pawn's tools).

## Values of the body (`body.json`)

| Name | Unit | Meaning |
| --- | --- | --- |
| `rig.pelvis`, `rig.chest`, `rig.head`, `rig.foot_left`, `rig.foot_right` | bone name | Bodies the balance assist holds. Text, read when the body starts; cannot change in a running body |
| `muscles.<group>.stiffness_nm_per_rad` | N·m/rad | Spring of every joint in the group toward the target pose. 0 switches the muscles off |
| `muscles.<group>.damping_s` | s | Damping as a time: damping torque = stiffness × this × angular velocity |
| `muscles.<group>.max_torque_nm` | N·m | Largest torque of a muscle. 0 = no cap, which is where all groups stand for now |
| `assist.<body>.horizontal_n_per_m`, `vertical_n_per_m` | N/m | Pull of the body toward its standing position |
| `assist.<body>.angular_nm_per_rad` | N·m/rad | Turn of the body toward its standing orientation |
| `assist.<body>.damping_s` | s | Damping of the three assist springs, as a time |
| `solver.position_iterations`, `velocity_iterations`, `projection_iterations` | count | Solver effort on the character's bodies. Whole numbers, at most 255 |
| `thresholds.flinch_impulse_ns` | N·s | A hit of at least this impulse makes a standing body flinch |
| `thresholds.flinch_window_s` | s | Impulses that arrive within this time of each other count as one hit (a push delivered over several frames) |
| `thresholds.fall_pelvis_offset_cm` | cm | Balance is lost once the pelvis is this far, horizontally, from where it stands. 15 cm is about the edge of the feet's support |
| `thresholds.fall_pelvis_drop_cm` | cm | ...or this far below its standing height (the legs gave way) |
| `thresholds.fall_tilt_deg` | degrees | ...or tilted this far from its standing orientation |
| `thresholds.rest_speed_mps`, `rest_angular_speed_rads` | m/s, rad/s | A fallen body is at rest when it moves and turns no faster than this as a whole (root mean square over its bodies, weighted by mass) |
| `thresholds.rest_time_s` | s | ...for this long |
| `reactions.flinch_weakening` | fraction | Stiffness of the hit region's muscles at the moment of a hit, as a fraction of normal |
| `reactions.flinch_recovery_s` | s | Time over which they return to full strength, in a straight line; also how long a flinch lasts |

Groups are `spine`, `neck`, `arms`, `legs`; assist bodies are `pelvis`, `chest`, `head`, `feet`. The ranges
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
`flinch`, `stagger`, `falling`, `lying` and `get_up`. The body has `standing`, `flinch`, `falling` and `lying`
so far; `stagger` and `get_up` come in M3.

| State | The body is | Leaves when |
| --- | --- | --- |
| `standing` | Upright on the base values | A hit reaches `thresholds.flinch_impulse_ns`; or the pelvis leaves its place by more than a fall threshold |
| `flinch` | Upright, the muscles of the hit region (spine, neck, arms or legs) weakened and recovering | `reactions.flinch_recovery_s` has passed; or a fall threshold is crossed |
| `falling` | Let go: no balance assist, weak muscles with strong damping | It has been at rest for `thresholds.rest_time_s` |
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
