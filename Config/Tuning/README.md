# Tuning

Every number that shapes how the body behaves lives in the `*.json` files of this directory. The code holds
no default values: a value missing here stops the body from starting, and a name the code does not know is
an error, so a misspelt one cannot pass unnoticed.

A value's name is its path in the file. In `body.json`

```json
{ "muscles": { "legs": { "stiffness_nm_per_rad": 800 } } }
```

is `muscles.legs.stiffness_nm_per_rad`. That name is used everywhere: here, in `-Tune`, in the console and
in `metrics.json`. Several files are merged into one set of names; a name may be defined only once.

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

Groups are `spine`, `neck`, `arms`, `legs`; assist bodies are `pelvis`, `chest`, `head`, `feet`. The ranges
that are accepted are in `FSandboxBodyTuning::Validate`; they catch a slipped decimal point, not physics.
The conversion from N·m/rad to engine units is taken from the engine source and has not been calibrated by
measurement (see `docs/testing.md`).

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
`flinch`, `stagger`, `falling`, `lying` and `get_up`. The files, overrides and checks understand this
section now; the body starts using it when the states themselves arrive (M2 step 3). Until then it reads the
base values only, so the file has no `states` section yet.

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
`FSandboxBodyTuning::DeclareParams` tells it the body's names, states and range checks.
`USandboxBodyController` compares the revision once per tick and re-reads its values when it moved.

Hot reload compares the files' size and modification time every 30 frames. It counts frames and never reads
the clock, and with unchanged files it does nothing, so it cannot disturb a scenario's repeatability. In a
scenario the runner does the looking, on fixed frames of the run, so that an edit made by the scenario
itself lands on the same frame every time.
