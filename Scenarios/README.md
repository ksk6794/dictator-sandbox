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
    "max_joint_stretch_cm": 3.0
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
| `body.control` | no, default `"passive"` | `"passive"` is a plain ragdoll. `"active"` adds muscles and balance assist: the character holds the pose it is spawned in. Spawn it with the feet on the floor (`location_cm` z = 0) |
| `settle_s` | no, default 2 | Start-up time left out of the "settled" metrics and expectations |
| `camera` | no | `{ "location_cm": [x, y, z], "look_at_cm": [x, y, z] }` for rendered runs. Without it the runner frames the body |
| `events` | no | Scripted stimuli, applied at simulated time `t` |
| `expect` | no | Pass criteria; omitted ones are not checked |

Event types. Unknown types are a parse error.

- `impulse` applies `impulse_ns` newton-seconds along `direction` (normalised for you) to the body on bone `body`.
- `pin` fixes the body on bone `body` at its current world position with a ball joint; the rest of the
  character hangs from it. `metrics.max_pin_error_cm` reports how far the body strayed from the pin point.

Expectations. A name that is not in this table is a parse error, so a misspelt one cannot pass unnoticed.

| Expectation | Passes when |
| --- | --- |
| `rest_within_s` | The body is at rest by that time and stays at rest |
| `max_joint_stretch_cm` | No rigid joint's anchors separate by more, measured from the sixth frame on |
| `max_pelvis_drift_cm` | The pelvis (the root body) never gets further from where it was spawned, over the whole run |
| `min_head_height_cm` | The head body's centre of mass never drops below this height above the spawn point. Active control only |
| `max_settled_speed_mps`, `max_settled_angular_speed_rads` | No body moves or turns faster after `settle_s`: the measure of jitter at rest |
| `max_joint_speed_rads` | No joint's two bodies rotate faster relative to each other, over the whole run |
| `max_physics_step_mean_ms` | The solver's mean wall-clock time per frame (all substeps) stays below it |

A physics asset can be authored with joint anchors that do not coincide in the reference pose; the solver
closes that gap in the first frames. It is reported as `metrics.initial_joint_offset_cm` and is not part of
the stretch metric.

Four checks always run: no NaN, the body stays within 50 m of the origin, every frame used the fixed
timestep, and every event found its body. With active control a fifth one checks that every joint of the
physics asset got a muscle.

## Tuning overrides

`Tools\scenario.ps1 -Name stand_npc -Tune "muscles.legs.stiffness_nm_per_rad=400,solver.position_iterations=32"`
changes body tuning values for one run. The names are those of `FSandboxBodyTuning`; an unknown name stops
the run. Every active run writes the full set of values it used to `control.tuning` in `metrics.json`.

## Output

Rendered runs add `performance` (GPU, render-thread and game-thread time per frame and the frame interval:
mean, median, 99th percentile, maximum, after a 90-frame warm-up) and `xr` (whether the run was in stereo,
the runtime, and the headset's render target size). In a headset the game-thread figure includes waiting
for the compositor, so use `gpu_ms` against the frame budget.

`metrics.json` has `pass`, the list of `checks` with value and limit, and these blocks:

- `metrics`: peak speeds, stretch, time to rest, drift of the pelvis and of the centre of mass, lowest head
  height, peak speeds after `settle_s`, the fastest joint.
- `body`: every body's mass and its peak speeds after `settle_s` (which body jitters), and every joint's
  parent and limits as instanced. Each joint also reports where the spawn pose sits against those limits
  (`spawn_twist_deg`, `spawn_swing1_deg`, `spawn_swing2_deg`) and the gap between its two anchors in the
  spawn pose (`spawn_anchor_offset_cm`).
- `control`: `passive` or `active`; for active runs the number of muscles and assist controls, the tuning
  values used, and `assist_load`. The load is the balance assist's commanded force as a fraction of body
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
