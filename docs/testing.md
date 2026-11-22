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

Correction made in M2: the stretch figures for the generated asset in the table above were measured while
the asset still carried the template's joint motors and anchor gaps on every joint, not only at `spine_02`.
Both are fixed now and the figures are superseded by the M2 step 1 section below.

### M2 step 1 (the character stands), 2026-11-22

Same machine and settings. The NPC (`SKM_SandboxNPC_BodyMesh` with the regenerated `PA_SandboxNPC`) is spawned
with its feet on the floor and held by `USandboxBodyController`: 15 muscles (one per joint, force-mode
springs toward the reference pose) and 5 balance-assist controls (pelvis, chest, head, both feet), with 64
solver position iterations on its bodies. Default tuning, bare floor, headless.

| Measure | `stand_npc` (10 s) | `stand_npc_10min` (600 s) | Limit |
| --- | --- | --- | --- |
| Pelvis drift from the spawn pose, maximum | 0.76 cm | 0.76 cm | 5 cm (M2 gate) |
| Pelvis drift at the end | 0.53 cm | 0.50 cm | |
| Lowest head height | 158.3 cm | 158.3 cm | 150 cm |
| Largest joint stretch | under 0.01 cm | under 0.01 cm | 2 cm (M2 gate) |
| Fastest body after 2 s, linear | 0.7 cm/s | 0.7 cm/s | 5 cm/s |
| Fastest body after 2 s, angular (a foot) | 0.07 rad/s | 0.13 rad/s | 0.3 rad/s |
| Assist load, force over body weight | 0.044 | 0.046 | |
| Assist load, torque over weight x centre-of-mass height | 0.0019 | 0.0020 | |
| Solver time per frame, mean | 0.40 ms | 0.39 ms | 1.5 ms (M2 gate) |
| Wall-clock time of the run | 1.1 s | 51 s | |

Both scenarios gave identical hashes in repeated headless runs and in a GPU-rendered run (see the table at
the end of this section). The 10-minute scenario simulates about 12 times faster than real time.

The assist-load figures are the baseline for the M3 criterion "standing assist share is at most half its M2
value". They are estimates: the solver does not report drive forces, so the load is computed from the assist
springs' settings and each body's distance from its target.

How much each part contributes, from 10-second runs with tuning overrides:

| Variant | Pelvis drift | Foot slide (left foot, sideways / forward) | Jitter (fastest foot) | Assist force share |
| --- | --- | --- | --- | --- |
| Default | 0.76 cm | 1.0 / 0.4 cm | 0.07 rad/s | 0.044 |
| 8 position iterations (engine default) | 0.98 cm | 4.0 / 5.2 cm and still sliding | 0.54 rad/s | 0.153 |
| 32 position iterations | 0.68 cm | 1.7 / 1.3 cm | 0.21 rad/s | 0.074 |
| No assist on the feet | 0.85 cm | 1.9 / 0.4 cm | 0.08 rad/s | 0.011 |
| No assist on pelvis, chest and head | 3.06 cm | 1.1 / 0.6 cm | 0.14 rad/s | 0.037 |
| No assist at all | 3.40 cm | 1.8 / 1.2 cm | 0.15 rad/s | 0 |
| No leg muscles | falls | | | |

With no assist at all the character still stands for the full 10 minutes (head at 158 cm) but walks 52 cm
across the floor on its vibrating feet. So at level 0 the legs carry the body and the assist mostly anchors it.

Solver cost against iterations (mean solver time per frame, one character on a bare floor): 0.26 ms at 8,
0.31 ms at 32, 0.40 ms at 64.

What it took to get here, in the order found. Each of the first three is a defect of the M1 physics asset
that the M1 scenarios could not show, because a body in free fall hides them:

1. **Joint anchors apart on every joint.** M1 recorded "a 3.6 cm offset at `spine_02`". That was the largest
   of 15: every joint had a gap of 1.5 to 3.6 cm, because the stock MetaHuman asset places its joints for
   another body's proportions. Closing the gaps shortened the skeleton by 12 cm in the first step. The
   generator now puts each parent-side anchor on the child-side one; the gap reads 0.000003 cm.
2. **Motors in the template.** Every stock joint has an orientation drive (stiffness 1000 to 5000) toward
   the zero of its axes, which is not the reference pose. They flexed elbows and knees: the hands moved
   30 cm in the first tenth of a second. The generator now clears them.
3. **Feet not flat.** The template's toe capsule sits 1.5 cm below the floor in the reference pose, so the
   foot pitched and the body leaned 6 degrees back through the stiff ankle. The feet are now boxes written
   out in the spec, with the sole on the floor. Pelvis drift went from 4.1 to 1.0 cm.
4. **Acceleration-mode drives.** Physics Control's default scales a drive by the inertia of the two jointed
   bodies, which made the load-bearing joints far too weak; the pose crept for as long as the run lasted.
   Muscles now use force mode, in N*m/rad.
5. **Solver convergence.** See the iteration rows above.

Effect of the corrected asset on the M1 scenarios (passive body, 8 iterations). The M1 figures were taken
with the template's motors still in the asset, which stiffened the body; these replace them:

| Scenario | Largest joint stretch, M1 | Now | Notes |
| --- | --- | --- | --- |
| `drop_npc` | 0.13 cm | 0.82 cm (right knee) | Rests after 2.4 s instead of 4.4 s |
| `room_drop_npc` | not recorded | 0.61 cm | Rests after 3.8 s |
| `hang_npc_hand` | 0.49 cm | 1.52 cm (right wrist) | M1 limit of 3 cm still met. The world pin gives by 9.7 cm (3.7 cm before) |

Not yet checked: the conversion from N*m/rad to the engine's units is taken from the engine source (unit
scale and Chaos's factor of 1.5 on angular drives), not from a calibration measurement. The arms' sag under
gravity is of the expected size (about 2 cm at the hand for 150 N*m/rad), which rules out an error of an
order of magnitude but not one of tens of percent.

Repeatability:

| Scenario | Runs compared | Result |
| --- | --- | --- |
| `stand_npc` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `stand_npc_10min` | 2 headless + 1 rendered | Identical hashes in all 3 runs (54,000 frames each) |

### M2 step 2 (the parameter file), 2026-11-22

Same machine and settings. The body's tuning now comes from `Config/Tuning/body.json` through
`FSandboxParamStore`, and every active run watches the tuning files (size and modification time, every 30
frames).

Nothing changed for a run that changes nothing. The whole suite was run before the step and after it, and
the hashes of all 13 earlier scenarios are equal. For the two stand scenarios:

| Scenario | Hashes before (values in code) | Hashes after (values from the file, files watched) |
| --- | --- | --- |
| `stand_npc` | bodies 55b1e27c, bones d4f09323 | the same |
| `stand_npc_10min` | bodies dde3a3e2, bones a38aa1a1 | the same |

So the numbers read from JSON are the same doubles the compiler made of the constants, and the 1,800 looks
at the files during the 10-minute run leave no trace. Their cost is not measurable either: the run takes
51.4 s of wall-clock time, as before (51.1 s).

A change in the middle of a run, by each route:

| Scenario | Route | Change | Reaches the body at | Result |
| --- | --- | --- | --- | --- |
| `tune_live_override` | Console command `Sandbox.Tune`, run at t = 4 s | Leg muscles 800 to 0 N*m/rad | t = 4.000 s (frame 360) | Pelvis from 90.0 cm to 17.3 cm; fallen at 5.10 s |
| `tune_live_file` | The value written into the file at t = 4 s; the session notices it | The same | t = 4.011 s (frame 361, after the look at frame 360) | The same; fallen at 5.11 s |
| `tune_live_solver` | Console command, run at t = 3 s | Position iterations 64 to 8 | t = 3.000 s (frame 270) | Feet start to rock: 0.54 rad/s after 4 s, against 0.07 unchanged |

Without leg muscles the character does not topple: it sinks and ends sitting on the floor with a straight
back, held there by the spine muscles and the upright assist (checked on a screenshot). Its head stays at
77 cm, which is why "fallen" is judged by the pelvis (below half its starting height), not by the head.
Joint stretch in these runs stays under 0.06 cm and the fastest joint reaches 9.2 rad/s.

Checks made once, with throw-away scenarios, that every kind of value applies to a running body:

| Changed at t = 3 s of a 10 s stand | Measure after t = 4 s | Changed live | Set from the start (`-Tune`) | Unchanged |
| --- | --- | --- | --- | --- |
| `solver.position_iterations` 64 to 8 | Fastest foot | 0.544 rad/s | 0.537 rad/s | 0.074 rad/s |
| | Assist force share | 0.147 | 0.153 | 0.044 |
| | Solver time per frame, median | 0.27 ms | 0.25 ms | 0.42 ms |
| All assist stiffness to 0 | Assist force share | 0 | 0 | 0.044 |
| A value out of range (`muscles.arms.stiffness_nm_per_rad=-1`) | Tuning changes | 0, with the reason in the log; hashes equal to the unchanged run | the run does not start | |

The first row did not hold at first. The live change of the iteration count was accepted by the store and
the controller and recorded as a tuning change, and the run's hashes were equal to the unchanged run: the
solver never saw it. Chaos takes a simulation island's iteration counts from its bodies only when something
joins or leaves the island, and a standing character's island never changes. The controller now adds a
constraint that restricts nothing for one frame whenever the counts change. `tune_live_solver` guards this
with a lower bound on the jitter, which only the reduced count reaches.

An edit from outside the process, once, by hand: `stand_npc_10min` was started on a copy of the tuning
files (`-TuningDir`), and 25 s later a script changed `muscles.arms.stiffness_nm_per_rad` from 150 to 120
in that copy. The run reported the reload 12 ms after the write, at 217.3 s of simulated time, and finished
with the new value. Where such an edit lands in simulated time depends on the wall clock, so a run that is
edited from outside is not repeatable; that is the nature of the edit, not of the watching.

What stops a run before it starts (exit code 2, with the reason): an unknown name in `-Tune`, a value out
of range, a misspelt name in a tuning file, a value missing from the tuning files.

Repeatability:

| Scenario | Runs compared | Result |
| --- | --- | --- |
| `stand_npc` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `stand_npc_10min` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `tune_live_override` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `tune_live_file` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `tune_live_solver` | 3 headless + 1 rendered | Identical hashes in all 4 runs |

`tune_live_file` is repeatable because the scenario runner looks at the files on fixed frames of the run
and the scenario writes the file itself at a fixed simulated time. Outside scenarios the store looks every
30 engine frames, counted from start-up.

Not measured: hot reload in a live interactive session (there is no pawn to run one with before step 4),
and per-state values on a body (the store resolves them and unit tests cover that; the body has no states
before step 3).

## Known behaviour of the stock mannequin

- **Residual hand motion.** After the 80 N*s impulse in `impulse_passive`, the right hand keeps rotating slowly
  on the floor and the body never satisfies the rest criterion (5 cm/s, 0.2 rad/s). The scenario therefore
  has no `rest_within_s` expectation. This is a property of the stock physics asset, to be addressed by the
  project's own physics asset in M1 and M2.
- **A free constraint.** `UserConstraint_0` in the stock physics asset is not a rigid joint; the stretch
  metric skips it.
