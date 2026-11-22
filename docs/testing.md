# Testing: repeatability policy and measurements

## Policy

1. **Same build, same machine, same scenario: results must be identical.** Every run writes two hashes
   (all body transforms and all bone transforms, every frame). `Tools\repeatability.ps1` runs a scenario
   several times and fails if any hash differs. A mismatch is a defect to investigate, not noise to tolerate.
2. **Headless and rendered runs must agree.** `-IncludeRendered` adds one GPU-rendered run to the comparison.
   If they diverge, something in the simulation depends on rendering (visibility-based pose updates are the
   usual cause).
3. **Pass criteria are physical thresholds, not hashes.** A scenario passes on its `expect` values and the
   built-in checks. Hashes are not committed as baselines, because a different CPU, compiler or engine hotfix
   may legitimately change floating-point results.
4. **Fallback if exact repeatability is lost.** Should a later feature make identical runs impossible (async
   physics is the likely one), switch the affected scenarios to a tolerance of 0.1 cm and 0.1 degrees at every
   checkpoint over three runs (`Tools\compare-runs.ps1 -PositionToleranceCm 0.1 -RotationToleranceDeg 0.1`),
   and record the reason here.

Re-measure at each milestone. M0 measured a passive ragdoll on a flat floor; muscles, effectors and props add
contacts and simulation islands, which is where parallel solving could start to matter.

## Measurements

### M0, 2026-11-22

Engine 5.8.4, MSVC 14.50.35726, Ryzen 7 9800X3D, RTX 5080. Synchronous physics, 90 Hz frame, 2 substeps.
Stock mannequin (`SKM_Manny_Simple`, 22 bodies, 74.8 kg).

| Scenario | Runs compared | Result |
| --- | --- | --- |
| `drop_passive` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `impulse_passive` | 3 headless + 1 rendered | Identical hashes in all 4 runs |

A full clean-state bootstrap between two measurement sessions also reproduced the same hashes.

Other numbers from these runs:

- A headless scenario simulates about 10 times faster than real time (6 s in 0.5 to 0.6 s); a whole
  scenario process, including engine start-up, takes about 8 s.
- Largest rigid-joint separation during a 1.5 m drop: 0.86 cm.

### M1 (in progress), 2026-11-22

Same machine and settings. MetaHuman body `SKM_SandboxNPC_BodyMesh`; the generated physics asset
`PA_SandboxNPC` has 16 bodies and 80.0 kg, the stock MetaHuman asset 21 bodies and 80.6 kg.

| Scenario | Physics asset | Largest joint stretch | Notes |
| --- | --- | --- | --- |
| `drop_passive` | Stock mannequin | 0.86 cm | |
| `drop_metahuman` | Stock MetaHuman | 0.14 cm | Rests after 5.3 s |
| `drop_npc` | Generated | 0.13 cm | Rests after 4.4 s |
| `hang_npc_hand` | Generated | 0.49 cm (right wrist) | Whole body hangs from the right hand for 10 s; M1 limit is 3 cm |

Headless and rendered runs of `drop_npc` and `hang_npc_hand` gave identical hashes.

Lighting: the test room (5 m by 4 m by 2.7 m, two point lights, 128-texel lightmaps) bakes from the command
line with CPU Lightmass at Preview quality in 90 s the first time and 12 to 17 s afterwards. No Lightmass
problems of the kind reported on the forums for 5.6 to 5.8 appeared. Higher qualities were not tried.

Two findings to carry forward:

- **The world pin itself gave by up to 3.7 cm** in `hang_npc_hand` (`max_pin_error_cm`), far more than any
  skeleton joint. A grab in M4 is the same kind of constraint, so it will need attention there (iterations,
  projection or a stiffer formulation).
- **An earlier reading of 3.6 cm stretch for the MetaHuman was wrong.** It was the authored anchor offset
  at `spine_02` being closed in the first frames, now reported separately as `initial_joint_offset_cm`.

## Known behaviour of the stock mannequin

- **Residual hand motion.** After the 80 N*s impulse in `impulse_passive`, the right hand keeps rotating slowly
  on the floor and the body never satisfies the rest criterion (5 cm/s, 0.2 rad/s). The scenario therefore
  has no `rest_within_s` expectation. This is a property of the stock physics asset, to be addressed by the
  project's own physics asset in M1 and M2.
- **A free constraint.** `UserConstraint_0` in the stock physics asset is not a rigid joint; the stretch
  metric skips it.
