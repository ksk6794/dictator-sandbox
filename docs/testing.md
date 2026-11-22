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

### M1, 2026-11-22

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

Flat launch with the OpenXR plugin enabled (headset off, SteamVR not running): `-nohmd` alone still let the
plugin create an OpenXR instance, which took 26.5 s and was then rejected as not viable. The run itself
passed with unchanged hashes and SteamVR was not left running. With `-DisablePlugins=OpenXR` added the same
rendered scenario takes 10.7 s end to end. Start-up time of a flat launch with the headset connected has
not been measured yet.

First run in the headset (PlayStation VR2 through SteamVR 2.18.4, OpenXR), scenario `vr_room_look`: the
MetaHuman standing in the baked test room, 30 simulated seconds, engine renderer settings untouched.

| Measure | Value |
| --- | --- |
| Render target chosen by SteamVR | 7456 x 3800 for both eyes (3728 x 3800 per eye, about 28 megapixels) |
| GPU time per frame | 19.7 ms mean, 22.1 ms at the 99th percentile |
| Frame interval | 22.4 ms mean: the headset ran at half rate (45 FPS) with reprojection |
| Render thread | 6.0 ms mean |
| Same scene flat at 1280 x 720 | 1.2 ms GPU |

This misses the 9 ms GPU budget by a factor of two, as expected before any VR tuning. Known contributors:
instanced stereo is off (the log says so), the render target is about three times the pixel count assumed
in the plan, no upscaler is in use, and SteamVR reported no visibility mask for the headset ("Failed to
create all visibility mask meshes"), so the hidden corners of each eye are shaded too.

Renderer sweep in the headset, scenario `vr_room_perf` (same scene, 15 simulated seconds, 1260 timed frames
per run). "Scale" is `xr.SecondaryScreenPercentage.HMDRenderTarget`, a percentage of SteamVR's target per axis.

| Anti-aliasing | Scale | Other | GPU ms, mean / p99 | Frame interval ms | Headset rate |
| --- | --- | --- | --- | --- | --- |
| TSR (engine default) | 100 | | 19.1 / 19.7 | 22.2 | 45 FPS |
| TAA | 100 | | 10.6 / 12.0 | 11.2 | 90 FPS, at the limit |
| TAA | 100 | instanced stereo | 10.9 / 11.5 | 11.7 | drops frames |
| TAA | 85 | | 7.5 / 7.9 | 11.1 | 90 FPS |
| TAA | 85 | instanced stereo | 7.6 / 8.0 | 11.1 | 90 FPS |
| TAA | 70 | | 5.0 / 5.4 | 11.1 | 90 FPS |
| TAA | 70 | no screen-space reflections, no ambient occlusion | 3.7 / 4.1 | 11.1 | 90 FPS |
| FXAA | 70 | | 4.9 / 5.2 | 11.1 | 90 FPS |

Conclusions:

- **TSR was the dominant cost**: about 8.5 ms of the 19 ms at the full target. The project default is now TAA.
- **The project default scale is 85%** (about 20 megapixels per frame, still 2.5 times the panels' pixel
  count). With both defaults and no overrides the scene measures 7.1 ms mean and 7.8 ms at the 99th
  percentile, inside the 9 ms budget, at a steady 90 FPS.
- **At SteamVR's full target the budget is missed** (10.6 ms).
- **Instanced stereo gained nothing** in this scene on GPU or render thread, and has open bugs in 5.8, so it
  stays off.
- These numbers are for a grey-box room and a MetaHuman without hair. Final materials, furniture, hair,
  the animated face rig and the active ragdoll will all add cost; the measured headroom is about 2 ms.
- DLSS has not been tried: the NVIDIA plugin is not installed.
- The owner viewed the scene in the headset with the project defaults (TAA, 85% scale) and judged the image
  sharp and acceptable, with the stutter of the 45 FPS runs gone.

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
