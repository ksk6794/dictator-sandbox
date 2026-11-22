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
| `fixed_fps` | no, default 90 | Engine frames per simulated second. Physics substeps come from `Config/DefaultEngine.ini` |
| `checkpoint_interval_s` | no, default 0.5 | How often body and bone transforms are written to `trace.json` |
| `body.mesh` | yes | Skeletal mesh object path |
| `body.physics_asset` | no | Overrides the mesh's own physics asset |
| `body.location_cm` | no, default `[0, 0, 150]` | Spawn location of the mesh origin (the feet). The floor's top is at z = 0 |
| `body.rotation_deg` | no | `[pitch, yaw, roll]` |
| `events` | no | Scripted stimuli, applied at simulated time `t` |
| `expect` | no | Pass criteria; omitted ones are not checked |

Event types (M0): `impulse` applies `impulse_ns` newton-seconds along `direction` (normalised for you) to
the body on bone `body`. Unknown types are a parse error.

Expectations (M0): `rest_within_s` (the body is at rest by that time and stays at rest),
`max_joint_stretch_cm` (largest anchor separation of any rigid joint over the whole run).

Four checks always run: no NaN, the body stays within 50 m of the origin, every frame used the fixed
timestep, and every event found its body.

## Output

`metrics.json` has `pass`, the list of `checks` with value and limit, `metrics` (peak speeds, stretch, time to
rest), `timing`, and two `hashes`: a CRC of all body transforms over every frame and one of all bone
transforms. Equal hashes mean two runs were identical in every frame. `trace.json` holds the transforms at
each checkpoint for measuring how far two runs drifted apart (`Tools\compare-runs.ps1`).
