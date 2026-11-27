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

Since M3 part 1 the solver's determinism mode is on (`p.Chaos.Solver.Deterministic=1`), after one run in
about 25 of a passive body had differed on a busy machine; see that section. Scenario processes may run
side by side: each has its own fixed timestep, and the suite gives the same hashes eight at a time as one
at a time.

Re-measure at each milestone. M0 measured a passive ragdoll on a flat floor; muscles, effectors and props add
contacts and simulation islands, which is where parallel solving could start to matter.

**How this journal is kept** (the owner's rule, 26 November 2026): it is a record of measurements, and its history is not rewritten. A section that a later section or a decision of the owner supersedes gets a dated note under its heading; a figure later found wrong gets a dated correction beside it, with a reference. Nothing else is changed.

**Where the files it names are** (the owner's decision of 26 November 2026). `Saved\` is not in git. The plans, patches and the one script that this journal and the plans cite as `Saved\Recon\<name>.md`, `.patch` or `.py` are kept in `docs/recon/<name>` since 26 November 2026; the paths below are left as they were written. Everything else under `Saved\` that the journal names (runs, batches such as `Saved\Recon\bat_*`, films, suites, baselines, backups) is on the reference machine only and is not on a clean clone.

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

### M2 step 3 (states and the push matrix), 2026-11-22

Same machine and settings. Three things changed: the joints of `PA_SandboxNPC` are built from anatomy
(`Specs/README.md`), the body has the states Standing, Flinch, Falling and Lying, and `Tools\push-matrix.ps1`
exists. Every hash of an NPC scenario changed with the joints; the figures below replace the earlier ones.

**Joints.** Where the reference pose sat in the joint limits, before and after:

| Joint | Before (stock axes): left / right | After: left / right |
| --- | --- | --- |
| Elbow | twist -40.0 / +32.9 of a limit of 70 | 36.6 degrees of bend on both sides, range 0 to 140 |
| Knee | twist -54.0 / -49.0 of a limit of 60 | straight on both sides, range -2 to 150 |
| Shoulder | swing1 -46.1 / -63.0, swing2 39.1 / 20.1 | identical on both sides |
| Hip | swing2 -20.0 / +20.0 (mirrored), swing1 40.0 / 40.0 | identical on both sides |

No joint is outside its limits in the reference pose, and every left and right pair reads the same to the
last digit (`spawn_*_deg` in `metrics.json`).

The knee's range is -2 to 150 degrees, not the plan's 0 to 150. With the limit at 0 a standing knee sits
exactly on it:

| Knee range | Fastest foot after 2 s, left / right | Pelvis drift, maximum |
| --- | --- | --- |
| 0 to 150 | 0.072 / 0.133 rad/s | 0.81 cm |
| -2 to 150 | 0.071 / 0.079 rad/s | 0.95 cm |
| -5 to 150 | 0.070 / 0.078 rad/s | 0.95 cm |
| -10 to 150 | 0.070 / 0.077 rad/s | 0.95 cm |

**Standing on the new joints** (default tuning, bare floor, headless):

| Measure | `stand_npc` (10 s) | `stand_npc_10min` (600 s) | Step 1 (10 s) | Limit |
| --- | --- | --- | --- | --- |
| Pelvis drift, maximum | 0.95 cm | 0.95 cm | 0.76 cm | 5 cm |
| Lowest head height | 158.3 cm | 158.3 cm | 158.3 cm | 150 cm |
| Largest joint stretch | under 0.01 cm | under 0.01 cm | under 0.01 cm | 2 cm |
| Fastest body after 2 s, angular (a foot) | 0.079 rad/s | 0.106 rad/s | 0.07 rad/s | 0.3 rad/s |
| Assist load, force over body weight | 0.047 | 0.051 | 0.044 | |
| Largest joint-limit violation | 0.01 degrees | 0.01 degrees | not measured | 1 degree |
| Solver time per frame, mean | 0.41 ms | 0.41 ms | 0.40 ms | 1.5 ms |

The state machine adds nothing measurable to a standing run: with it and without it the hashes of
`stand_npc` were equal.

**States.** Scenarios of the suite, with the time each state began:

| Scenario | Stimulus | States | Notes |
| --- | --- | --- | --- |
| `hit_chest_10_stands` | 10 N*s blow on the chest at 3 s | flinch 3.00 s, standing 3.36 s | Pelvis moves 1.2 cm |
| `hit_head_30_stands` | 30 N*s blow on the head | flinch 3.00 s, standing 3.36 s | Neck turns at up to 13.0 rad/s; pelvis moves 4.3 cm |
| `hit_chest_200_falls` | 200 N*s blow on the chest | flinch 3.00 s, falling 3.08 s, lying 4.14 s | Fallen (pelvis below half height) at 3.40 s; joint stretch 0.04 cm |
| `push_thigh_200_falls` | 200 N*s on the left thigh over 0.1 s | flinch 3.00 s, falling 3.13 s, lying 5.38 s | Joint stretch 0.16 cm; a hip 4.7 degrees past its limit |
| `tune_live_override` | Leg muscles switched off at 4 s | falling 4.42 s, lying 6.22 s | Falling because the pelvis sank 20 cm; before step 3 this body ended sitting upright |
| `hang_npc_hand` | Pinned by the right hand in the air | falling 0.38 s | Never at rest: it swings |

Screenshots of `hit_chest_200_falls` (on its back, limbs loosely straight) and of the passive `drop_npc`
(prone, one leg bent) show no joint bent the wrong way.

What the flinch does, measured on `hit_head_30_stands`: with the hit muscles weakened to 30% the neck turns
at up to 13.0 rad/s; with `reactions.flinch_weakening=1` (no weakening) at 9.8 rad/s. The body stands in
both runs.

**Push matrix.** 128 runs, 8 at a time, 169 s. Each cell is how many of the 8 directions ended in a fall;
the M2 rules are the first and the last column.

Single blow (the default, and what `Tools\bootstrap.ps1` runs):

| Body | 10 N*s | 30 N*s | 80 N*s | 200 N*s |
| --- | --- | --- | --- | --- |
| Head (`neck_01`) | 0 of 8 | 0 of 8 | 8 of 8 | 8 of 8 |
| Chest (`spine_04`) | 0 of 8 | 0 of 8 | 1 of 8 (straight back) | 8 of 8 |
| Left thigh | 0 of 8 | 0 of 8 | 0 of 8 | 8 of 8 |
| Right thigh | 0 of 8 | 0 of 8 | 0 of 8 | 8 of 8 |
| Pelvis movement of those that stood | 1 to 3 cm | 2 to 8 cm | 7 to 14 cm | |
| Fastest joint | 4 rad/s | 13 rad/s | 31 rad/s | 40 rad/s |
| Largest joint stretch | 0.01 cm | 0.01 cm | 0.12 cm | 0.31 cm |

Every mirror-image pair (the same push reflected left to right, on the other side's body) ended the same
way: 56 pairs, none different. The same impulses delivered as pushes over 0.1 s (`-PushSeconds 0.1`,
measured before the knee's range was changed) gave the same table of falls, with a fastest joint of
32 rad/s and a largest stretch of 0.18 cm.

How the fall threshold was set. With `thresholds.fall_pelvis_offset_cm` at 20, a 200 N*s push on a thigh
moved the pelvis by 18.4 to 19.6 cm and the character came back upright in 12 of 16 cases: the feet are
held by their assist and the legs are stiff. At 15 cm, which is about where the centre of mass leaves the
feet's support, every 200 N*s push ends in a fall and 80 N*s on the chest still stands in 7 directions of 8.
The outcomes for 30 and 80 N*s are recorded, not judged: the outcome table is an M3 criterion.

**Solver iterations decide the stretch under load.** Hanging from one hand, 10 s:

| Body | Position iterations | Largest joint stretch (wrist) | The pin gives by |
| --- | --- | --- | --- |
| Passive, new joints | 8 (engine default) | 2.88 cm | 9.0 cm |
| Passive, new joints | 64 (console variable) | 0.51 cm | 2.0 cm |
| The NPC under active control (falls limp at 0.38 s) | 64 (its own) | 0.13 cm | 0.5 cm |

`hang_npc_hand` now runs the third row, with a limit of 2 cm: a passive NPC does not exist in the product.
This supersedes the "world pin gives by 9.7 cm" finding of step 1 for the NPC proper; a grab in M4 starts
from 0.5 cm.

**Passive bodies overrun their joint limits.** The new metric shows a passive body dropped on the floor
pushing a joint 36 to 40 degrees past its limit (`drop_npc`, `room_drop_npc`), against 0.2 to 4.7 degrees
for the NPC under control in the fall scenarios. The limits are soft (stiffness 50, damping 5, still the
template's) and a passive body has 8 iterations. Not a problem for the NPC as it runs, but the softness is
untuned.

Repeatability, final build of the step:

| Scenario | Runs compared | Result |
| --- | --- | --- |
| `stand_npc` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `stand_npc_10min` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `hit_chest_10_stands` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `hit_chest_200_falls` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `push_thigh_200_falls` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `hang_npc_hand` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `drop_npc` | 3 headless + 1 rendered | Identical hashes in all 4 runs |

The matrix runs eight scenario processes side by side; each uses a fixed timestep, so its result does not
depend on the others. `hit_chest_200_falls` and `push_thigh_200_falls` gave the same hashes inside the
suite and alone.

`Tools\bootstrap.ps1` now takes about 7.5 minutes: the suite of 20 scenarios 4.3 minutes, the push matrix
2.8 minutes.

Not measured: hits by another body (there are no effectors before step 4), the flinch's effect on anything
but the neck, and falls against walls or furniture.

### M2 step 4 (desktop pawn with effectors and tools), 2026-11-22

Same machine and settings. New in this step: the player's hands (effectors), the character's sense of
contact, the `effector` scenario event, the desktop pawn and the interactive session (`Tools\play.ps1`).

**Nothing changed for a run without hands.** The whole suite was run before the step and after it. All 20
earlier scenarios give the same two hashes, although the character now receives contact events from the
physics scene in every frame, the game mode has a new pawn class, and the scene is built by shared code.
`room_stand_npc`, new, gives the same hashes as `stand_npc`: the test room's floor is the bare floor to the
solver. The solver's mean time per frame is unchanged (`stand_npc` 0.41 ms before and after).

**The force cap, measured from the other side.** A hand's spring may apply 300 N. In `effector_force_cap`
the hand's target is moved 30 cm below the floor and left there; the hand comes to rest on the floor and
the floor's contact events report what it presses with:

| Measure | Value |
| --- | --- |
| Contact force in the run's last frame | 300.000 N (cap 300 N) |
| Largest contact force of any frame | 300.0 N |
| Hand's distance from its target | 34.5 cm; the spring law asks for 1157 N at most |
| Same, with the cap changed to 100 N at 4 s from the console (`tune_live_effector`) | 100.000 N |

This is one measurement of three things: the cap holds, the conversion of newtons into the engine's units
is right, and the impulses of the contact events (summed over the two substeps of a frame) are the real
ones. The hand is alone in its simulation island here, so this is the engine's 8 position iterations.

The engine caps a drive axis by axis. Without correction a spring with one cap would be 1.41 times stronger
along a face diagonal and 1.73 times along a space diagonal. The caps are therefore set every frame along
the direction of the force asked for. Checked once with a throw-away scenario: two free hands whose targets
jump 100 cm in one frame, one along x and one along the space diagonal.

| Hand | Peak speed |
| --- | --- |
| Target jumps 100 cm along x | 11.7968 m/s |
| Target jumps 100 cm along (-1, 1, 1) | 11.7968 m/s |
| Along x, mass 3 kg from the start (`-Tune`) | 9.6627 m/s |
| Along x, mass changed from 1.5 to 3 kg at 1 s, a second before the jump | 9.6627 m/s, and the same hashes as the row above |

The last two rows are the check that a value made tunable at run time reaches the solver: the run with the
live change is identical to the run that had the value from the start.

**Hands and the character.** Default tuning (hand 1.5 kg, 3000 N/m, 300 N), bare floor, headless. The hand's
figures come from its own contact events, the character's from the character's.

| Scenario | The hand's target | Hand delivers | Character feels | States | Notes |
| --- | --- | --- | --- | --- | --- |
| `effector_push_chest` | Into the chest at 2.5 m/s and straight back | 17.75 N*s in 5 frames, peak 540 N | 17.75 N*s in 9 reports | flinch 3.20 s (hit of 6.0 N*s on `spine_04`), standing 3.57 s | Pelvis moves 2.2 cm |
| `effector_hit_head` | Into the side of the head at 7.5 m/s | 14.10 N*s in 1 frame, peak 1269 N | 14.10 N*s in 1 report | flinch 3.08 s (hit of 14.1 N*s on `neck_01`), standing 3.44 s | Hand reaches 8.28 m/s; neck turns at 6.2 rad/s |
| `effector_push_chest_falls` | Into the chest and on through it for 1 s | 114.07 N*s in 46 frames, peak 503 N | 114.07 N*s in 91 reports | flinch 3.24 s, standing 3.61 s, flinch 3.61 s, falling 3.64 s (pelvis 15.1 cm away), lying 5.06 s | Joint stretch 0.02 cm; fastest joint 7.6 rad/s |
| `effector_miss` | Across in front of the chest, not touching | 0 | 0 | standing | Pelvis drift 0.95 cm, as in `stand_npc` |

What the two sides count agrees to the last digit, as it should: it is the same contact seen from both
bodies. The character knows nothing of the hand. It counts every contact with another simulated body and
leaves out the static world, because the floor under its feet reports 8.7 N*s per frame, more than the
flinch threshold of 5.

Two things these runs show that the plan did not say:

- **The peak contact force is not held by the force cap** (540, 1269 and 503 N against a cap of 300). A
  hand that is stopped gives up its momentum within a frame, on top of what the spring adds: 1.5 kg at
  8.3 m/s is 12.4 N*s. The cap is a limit on sustained force, and is checked that way above.
- **A held push makes the character flinch again the moment a flinch ends** (3.61 s in the third row). Each
  frame of contact adds to the felt impulse, and the threshold is reached again within two frames. The same
  would happen with an `impulse` push longer than a flinch. Left as it is: what a sustained push should do
  to the muscles is M3.

Largest joint stretch in the six hand scenarios: 0.02 cm. Largest joint-limit violation: 0.1 degrees.

**Cost.** One hand in the scene adds 0.03 ms to the solver's mean time per frame (`effector_miss` 0.44 ms
against `stand_npc` 0.41 ms); the most expensive run of the suite is `effector_push_chest_falls` at
0.46 ms. The M2 limit is 1.5 ms.

**The interactive session.** `Tools\play.ps1 -Smoke` runs the session without a person: it uses each tool
through the function its key calls and asks the world what happened. 480 frames, 13 checks, all pass,
headless and rendered:

| Check | Result |
| --- | --- |
| The pawn has two hands, resting on their targets | 0.00 cm from target |
| Impulse tool, one click on the chest | flinch from a "hit of 30.0 N*s": the same entry as a scenario's `impulse` |
| Pause | 0 frames simulated in 30 paused frames; the pelvis transform is bit-equal |
| Single step | exactly 1 frame simulated |
| Slow motion, divisor 4 | 20 frames simulated in 80 |
| Drag handle on the left forearm, camera raised 30 cm | holds `lowerarm_l`; the held point rises 22.4 cm and stays 7.7 cm behind its target (the arm's muscles pull back, the handle's force is capped) |
| Right hand reaches into the chest | the hand delivers 38.0 N*s, the character feels 38.0 N*s and flinches |
| Reset | a fresh character, standing, pelvis 1.5 cm from where the first one started |

A window opened by `Tools\play.ps1` and left alone for 30 s logged 900 frames in 10.00 s and 900 frames in
10.01 s by the clock, with no error in the log: the session holds the simulation's 90 frames per second.
`-Smoke -Shot` gives a picture of the session's view: the two hands below the crosshair, the HUD lines, the
character in the room.

Repeatability, final build of the step:

| Scenario | Runs compared | Result |
| --- | --- | --- |
| `effector_force_cap` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `effector_push_chest` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `effector_hit_head` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `effector_push_chest_falls` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `effector_miss` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `tune_live_effector` | 3 headless + 1 rendered | Identical hashes in all 4 runs |

The hands' transforms are part of the `bodies` hash of a run that has hands.

`Tools\bootstrap.ps1` now takes about 8.5 minutes: the suite of 27 scenarios 5.1 minutes, the push matrix
2.9 minutes, the session's self-test 10 seconds. The push matrix gives the same table and figures as in
step 3.

**For a person to check**, because no run without one can: that the keys and the mouse do what the HUD
says; that the camera turns the right way up and down; that the hands feel weighted and do not launch the
character; that the drag handle is usable; that slow motion is readable; and hot reload in a live session
(save `Config/Tuning/effectors.json` with another `max_force_n` while the window is open and see the hands
change, with "Effectors: new tuning values applied" in the log). The steps are in the report of the step.

Not measured: strikes at full speed and tunnelling (step 5; continuous collision detection is on for the
hands but nothing here tests it), a hand against a fallen character, two hands at once, anything in the
headset.

### M2 step 5 (overlays, strikes and the M2 gate figures), 2026-11-22

Same machine and settings. New in this step: debug overlays (`Sandbox.Overlay`), seven strike scenarios, the
runner's own measurement of penetration and tunnelling, and two limits that `Tools\suite.ps1` holds the
whole set to.

**Nothing changed for the runs that existed.** The suite was run on the build of step 4 before any change
and on the final build: all 27 earlier scenarios give the same two hashes. That includes one change to the
body itself (the character's bodies now ask for contact events, see below).

**Strikes.** A hand's target is sent in a straight line through a place on the standing character at 6 m/s
(slap) or 8 m/s (punch) and back. Default tuning (hand 1.5 kg, 300 N), bare floor, headless. The hand
arrives faster than its target moves, because it starts behind and has to catch up.

| Scenario | Hand arrives at | Touches first | Hand delivers | Peak force | Struck body moves | Fastest joint | Deepest penetration |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `strike_slap_head` (from the side) | 6.25 m/s | `neck_01` | 11.3 N*s in 1 frame | 1017 N | 7.9 cm | 6.0 rad/s (neck) | 0.000 cm |
| `strike_punch_head` (from the front) | 8.87 m/s | `neck_01` | 15.7 N*s in 1 frame | 1415 N | 13.2 cm | 5.8 rad/s (neck) | 0.000 cm |
| `strike_slap_torso` | 6.25 m/s | `spine_04` | 15.2 N*s in 5 frames | 956 N | 4.9 cm | 1.6 rad/s (neck) | 0.000 cm |
| `strike_punch_torso` | 8.95 m/s | `spine_04` | 29.3 N*s in 8 frames | 1394 N | 6.9 cm | 3.6 rad/s (chest) | 0.003 cm |
| `strike_slap_leg` (outside of the left thigh) | 6.25 m/s | `thigh_l` | 21.9 N*s in 6 frames | 1325 N | 1.4 cm | 2.8 rad/s (ankle) | 0.004 cm |
| `strike_punch_leg` (front of the right thigh) | 8.87 m/s | `thigh_r` | 28.2 N*s in 6 frames | 1859 N | 2.0 cm | 2.6 rad/s (knee) | 0.010 cm |
| `strike_punch_forearm` | 8.87 m/s | `lowerarm_l` | 13.7 N*s in 2 frames | 962 N | 22.0 cm | 10.1 rad/s (shoulder) | 0.002 cm |

In all seven: no tunnelling; the character flinches (3.07 to 3.09 s) and is standing again 0.36 to 0.37 s
later;
the pelvis moves 1.4 to 3.3 cm; joint stretch is 0.03 cm at most and no joint is more than 0.1 degrees
outside its limits. What the hand delivered and what the character felt agree to the last digit, as in
step 4. The M2 limits are no tunnelling and 30 rad/s; the fastest joint in any strike turns at a third of
that.

The thresholds of the scenarios for "the struck region is visibly displaced" are a choice made here, not a
given: 5 cm for the head, 3 cm for the chest, 1 cm for a thigh (measured: 7.9 and 13.2, 4.9 and 6.9, 1.4
and 2.0). A thigh is 11.3 kg on a leg that stands on the floor; a slap does not move it far.

**Tunnelling and penetration: how they are measured.** After every frame the runner asks the scene, with
queries that change nothing: how deep is the hand's sphere inside each body of the character and inside
the static world (the distance it would have to move to be clear), and does the straight line from where
the hand's centre was a frame ago to where it is now cross a surface. The centre of a sphere cannot get
closer to a surface than its radius, so a crossing, or a depth of the radius or more, means the hand went
through; such frames are counted (`tunnel_frames`) and any of them fails the run
(`effectors_not_tunnelled`, checked in every run that has hands). The solver's own contacts are not used:
what it missed it cannot report. The measurement is taken once per frame, so what happens between two
substeps is not seen.

In the six hand scenarios of step 4 the same measurement reads 0 tunnelled frames and 0.004 cm at most.

**The metric fails when it should, and continuous collision detection is what keeps it from failing.**
A check made once: the strike scenarios with the target's speed multiplied, the hand's force cap raised to
6000 N so that it can follow, and CCD on or off (`-CVars "p.Chaos.Solver.UseCCD=0"`). The hand's speed is
what it reached, not what was asked for.

| Hand's speed | CCD | Deepest penetration, worst of the seven | Tunnelled |
| --- | --- | --- | --- |
| 6.25 and 8.87 m/s (the scenarios as they are) | on | 0.01 cm | no |
| the same | off | 0.01 cm | no |
| 11.8 to 15.8 m/s | on | 0.03 cm | no |
| the same | off | 1.88 cm (chest) | no |
| 16.6 to 22.1 m/s | on | 0.01 cm | no |
| the same | off | 2.26 cm (chest) | no |
| 18.8 to 25.1 m/s | on | 0.02 cm | no |
| the same | off | 1.68 cm (head) | no |
| 24.6 to 28.1 m/s | on | 0.01 cm | no |
| the same | off | 6.54 cm (chest), deeper than the hand's 4.5 cm radius | yes: `effectors_not_tunnelled` fails |

So at strike speeds the hand does not tunnel with or without CCD. With CCD nothing tunnels up to 28 m/s,
three times the punch's speed; without it the hand sinks 1.9 cm into the chest from 15 m/s and goes through
at 25 m/s. CCD on the hands stays on. Two more things these runs show:

- **Below 6.5 m/s CCD is not in play at all.** Chaos sweeps a body only when it moves more than 0.4 of its
  smallest extent in a substep, 3.6 cm for the 9 cm hand. The two slaps give the same hashes with CCD off;
  the punches do not.
- **Far above strike speeds CCD costs momentum.** From 22 m/s some hits with CCD deliver about half the
  impulse of the same hit without it (11.4 against 23.7 N*s on the head at 25 m/s), and the hand's speed at
  the first contact reads half its peak. Nothing a hand or a 10 m/s kick comes near, but a thrown prop
  could.

At three times the punch's speed joints do pass 30 rad/s: 34 rad/s in the shoulder after a forearm hit at
25 m/s, and 179 rad/s in the wrist in one run. The limit is met with a factor of three at 8.9 m/s, not at
any speed.

**The character did not hear its own contacts.** The overlay had no contacts between the feet and the floor
to draw, and a character knocked down onto the floor reported none either. The cause is in how the engine
builds a body's collision filter: a body of a skeletal mesh asks for contact events only if its own flag
and the flag of the component's body instance are both set, and `USkeletalMeshComponent::
SetNotifyRigidBodyCollision` sets only the first. So since step 4 the character had been told of a contact
only when the other body asked for events itself. The hands do, which is why every hand scenario agreed to
the last digit; the floor does not, and a thrown prop would not have. The controller now sets both flags.
After the change each foot reports 4.4 N*s per frame against the floor (two reports, one per substep):
392 N per foot, half the character's weight. The character still counts only contacts with bodies that
move under physics as hits, so nothing it does changed: all hashes are equal, and the solver's mean time is
the same (0.40 ms in `stand_npc`).

**Overlays.** Seven layers: centre of mass, support polygon, joint drive targets against the pose, balance
assist, contacts, controller state, hands. They are drawn by a subsystem that only reads. Checked on the
whole suite:

| Run | Scenarios | Hashes equal to the plain headless run |
| --- | --- | --- |
| Headless, `-Overlay all` (the overlay says it has no renderer and stays off) | 34 | 34 |
| Rendered, `-Overlay all` (the overlay draws every frame) | 34 | 34 |

Pictures looked at: `strike_punch_torso` at the frame of the hit (the arrow of the contact on the chest
with its force, the flinch named above the head, the support polygon under both feet with the two arrows
of the floor's reaction, the centre of mass 9.4 cm inside the polygon) and the session's view from
`Tools\play.ps1 -Smoke -Shot -Overlay all`. The session's self-test has a check for them in each mode: with
a renderer the V key's command gives 147 line segments and 8 lines of text; without one it gives nothing.

**A reset logged warnings nobody read.** The session's self-test passed while its log held 30 warnings of
Physics Control ("Failed to find bone data", two per muscle) in the frame of the reset. The new character is
spawned in the middle of a frame and its controller was ticked in that same frame, before the mesh had any
bone transforms. The controller now waits for the mesh's first pose: 0 warnings, and the reset check reads
as before (pelvis 1.52 cm from where the first character started). Scenarios spawn their body before the
world's first tick and never had the warnings.

**Repeatability.** The runs below were made one change before the final build (the controller's wait for
the mesh's first pose, above); the final build gives the same hashes for all 34 scenarios.

| Scenario | Runs compared | Result |
| --- | --- | --- |
| `strike_slap_head` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `strike_punch_head` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `strike_slap_torso` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `strike_punch_torso` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `strike_slap_leg` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `strike_punch_leg` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `strike_punch_forearm` | 3 headless + 1 rendered | Identical hashes in all 4 runs |

**The limits for the whole set** (`Tools\suite.ps1`, 34 scenarios, 375 s):

| Limit (M2 gate) | Worst scenario | Value |
| --- | --- | --- |
| Joint stretch of a simulated body at most 2 cm | `impulse_passive` (stock mannequin, 8 solver iterations) | 0.86 cm |
| | worst with the NPC under control: `push_thigh_200_falls` | 0.16 cm |
| Mean physics step at most 1.5 ms | a hand scenario (`strike_punch_head` in one run, `effector_hit_head` in the next) | 0.45 ms |

Five scenarios have a body that is not simulated (portraits and the headset scenes) and no stretch figure:
nothing solves their joints. Two of them, `face_metahuman` and `portrait_metahuman`, used to report
3.6 cm, which is the gap between the anchors the stock MetaHuman asset is authored with; it is still
reported, as `initial_joint_offset_cm`.

Fastest joint by kind of stimulus, for the question of what the 30 rad/s limit applies to: strikes by a
hand 10.1 rad/s; calibrated blows on the NPC 13 rad/s at 30 N*s, 31 at 80 and 40 at 200 (push matrix, step
3); a passive body dropped on the floor 34 to 52 rad/s (`drop_npc`, `room_drop_npc`, `drop_passive`).

Decided by the owner on 23 November 2026: the 30 rad/s limit applies to strikes by a hand, and the other
stimuli record the figure without being judged by it; a body that is not simulated has no stretch figure,
and the portrait scenarios stay on the stock asset.

**Still for a person to check.** The list of step 4 has not been gone through yet; the owner deferred it on
23 November 2026 and M3 starts without it: keys and mouse, the direction the camera turns up and down, the feel of the hands, the drag handle, slow
motion, hot reload in a live window. New with this step: the V key and the overlays in a live session
(`Tools\play.ps1 -Overlay all`).

The suite now takes 6.3 minutes one scenario after another, against the 5 minutes NFR-6 allows for build
and suite together. The scenarios do not depend on each other: all 34 run in 91 s eight at a time, with the
same hashes. `Tools\suite.ps1` has not been made parallel, because its physics-step limit is a wall-clock
measurement.

`Tools\bootstrap.ps1` takes 9.6 minutes: the suite 6.2 minutes, the push matrix 2.8 minutes, the unit tests
(25) 15 s, the session's self-test (14 checks) 10 s. The push matrix gives the same table and figures as in
step 3 (worst joint stretch 0.31 cm, fastest joint 39.6 rad/s).

Not measured: strikes on a character that is lying or falling, two hands at once, a strike against a
character near a wall, penetration between the character's own bodies and between the character and the
floor (the fall-and-rest family, M3), anything in the headset (overlay text is not drawn there).

### M3 part 1 (senses, metrics and tools; three corrections), 2026-11-23

Same machine and settings. The part was made in two steps. Step A added what the body senses of its balance,
the measurements of energy, rest and penetration, the `capture` overlay layer, and the tools (the suite in
parallel, baselines, sweeps), and was to change nothing a run does. Step B corrected three things step A
brought out: the generated physics asset had no self-collision, the standing character slept one substep
in six, and the solver's contact order depended on thread scheduling.

**Step A changed nothing for the runs that existed.** The suite was run on the committed M2 state before
any change (the baseline `m3_start`) and on the build of step A:

| Compared | Result |
| --- | --- |
| 34 scenarios, headless | The same two hashes in all 34 |
| Push matrix, 128 runs | The same outcome, state sequence and pelvis drift in all 128 |
| `stand_npc`, `hit_chest_200_falls`: 2 headless runs and 1 rendered | Identical |
| `hit_head_30_stands`, `hit_chest_200_falls` rendered with `-Overlay all` | The hashes of the headless runs |

So the body's new sense (it now keeps the floor's contact impulses and works out its balance every tick),
the scene queries after every frame and two new tuning values leave no trace in a run.

**Running the set eight at a time.** One after another the 34 scenarios took 393 s; eight at a time they
take 95 to 100 s, with the same hashes. The physics step, which is timed by the clock, reads longer when
the runs share the processor:

| Scenario | Mean physics step alone | Sharing the processor with seven runs |
| --- | --- | --- |
| `stand_npc` | 0.40 ms | 0.58 to 0.77 ms |
| `hit_chest_200_falls` | 0.44 ms | 0.68 to 0.75 ms |
| worst of the set | 0.45 ms | 0.75 to 0.95 ms |

The limit of 1.5 ms is still far off, and a scenario that does read over a step limit in the parallel pass
is run again alone before it is judged. Figures written down in this file are from runs one at a time.

**What the new figures said about the body as it was** (before step B):

| Figure | Value | Remark |
| --- | --- | --- |
| Centre of mass inside the support polygon, standing | 9.3 cm | The overlay of M2 step 5 read 9.4 cm |
| The floor's push on both feet over body weight, standing | 0.83 | Should be 1: the first sign of the sleep below |
| Bodies of the NPC that may collide with each other | 0 pairs of 120 | The asset had no self-collision |
| Rest after the fall began, the three falls of the suite | 0.57, 2.36 and 0.91 s | Limit 3 s |
| Energy gained by a falling or lying NPC | 0.02 to 0.10 J | An 80 kg body lifted by 1 cm is 7.8 J |
| Energy gained by a passive body dropped from 1.5 m | 171 J (`drop_npc`), 74 J (`room_drop_npc`), 77 J (`drop_metahuman`) | It lands with a foot 8.4, 5.1 and 1.6 cm inside the floor and is pushed out again |

The last row is the measurement the decision about continuous collision detection on the feet will rest
on (part 5). The stock mannequin lands 4.1 cm deep and gains nothing (`drop_passive`).

**The standing character slept one substep in six.** The feet's contact reports of three successive frames
of `stand_npc`, with the solver's time for the frame:

| Frame | Reports of the left foot against the floor | Force over the frame | Solver time |
| --- | --- | --- | --- |
| 297 | 1 (2.13 N*s) | 192 N | 0.27 ms |
| 298 | 2 (2.11 and 2.12 N*s) | 381 N | 0.44 ms |
| 299 | 2 (2.13 and 2.14 N*s) | 384 N | 0.44 ms |

The pattern repeats every three frames. A body in free fall gains 0.1089 m/s per frame, which is 9.80 m/s^2,
so no simulated time is lost in general; and each report that does arrive carries the full weight of one
substep. What is missing is a whole substep: the solver has put the character's island to sleep, with no
gravity, no contact and no contact event, and the controller's next tick wakes it. With
`-CVars "p.Chaos.Solver.Sleep.Enabled=0"` there are two reports in every frame and the feet carry 0.999 of
the weight. The controller now sets its bodies to never sleep, which gives the same hashes as that switch.

**The NPC had no self-collision.** Read back from the asset at run time, all 120 pairs of its 16 bodies
were switched off. Measured over all pairs that are not jointed, whatever the asset says: after the drop of
`drop_npc` the left forearm was 16.8 cm inside the chest. In the spawn pose four such pairs overlap:

| Pair | Overlap in the spawn pose |
| --- | --- |
| Pelvis and chest (`spine_04`) | 5.5 cm |
| The two thighs | 1.4 cm |
| Lower spine (`spine_02`) and each thigh | 0.9 cm |

The template's own table was tried first (70 pairs on) and dropped: it is not the same left and right (an
upper arm collides with its hand on one side only), and it leaves the thighs colliding, which made the
standing character's feet turn at 1.26 rad/s and raised the assist's load to 0.216 of body weight. The rule
now: every pair collides except the 15 jointed ones and the four above, 101 pairs.

**Step B, first change: self-collision.** Against the results of step A, 28 scenarios kept their hashes,
among them every standing one, every strike but one, and the falls in which no limb meets the body. Six
changed:

| Scenario | What changed |
| --- | --- |
| `drop_npc`, `room_drop_npc` | Limbs now rest on the body: self-penetration 0.38 and 0.65 cm; fastest joint 34 to 53 and 40 to 42 rad/s (a passive body, not judged) |
| `push_thigh_200_falls` | Rest after the fall 2.36 to 2.74 s; a hip 4.7 to 1.5 degrees past its limit; self-penetration 0.11 cm |
| `strike_punch_forearm` | The struck forearm now meets the body: fastest body 10.1 to 10.4 rad/s |
| `tune_live_file`, `tune_live_override` | The character that sits down when its leg muscles are switched off: rest 1.30 to 1.34 s |

**Step B, second change: a body under control never sleeps.** All 23 scenarios with a controlled body that
stands at some time changed (`hang_npc_hand`, which swings from the first frames, did not); the 10 without
one did not. A standing body, before and after:

| Measure | Sleeping one substep in six | Awake | Limit |
| --- | --- | --- | --- |
| Pelvis drift, maximum | 0.95 cm | 0.97 cm | 5 cm |
| Fastest body after 2 s (a foot), 10 s run | 0.079 rad/s | 0.150 rad/s | 0.3 rad/s |
| The same, 10 minute run | 0.106 rad/s | 0.158 rad/s | 0.3 rad/s |
| Assist load, force over body weight, 10 s and 10 minutes | 0.047 and 0.051 | 0.0185 and 0.0185 | |
| Assist load, torque over weight x centre-of-mass height | 0.0017 | 0.0020 | |
| The floor's push on both feet over body weight | 0.83 | 0.999 | |
| Centre of mass and capture point inside the support polygon | 9.3 cm | 9.9 cm | |
| Solver time per frame, mean | 0.40 ms | 0.45 ms | 1.5 ms |

**The M2 assist baseline was inflated by the sleep.** The M3 criterion "standing assist share is at most half
its M2 value" was written against 0.047 (and 0.051 over ten minutes). Awake, the same body with the same
tuning reads 0.0185. Half of the M2 figure is 0.0235, which the body now meets with no work done; half of
the corrected figure is 0.0093. Which one the criterion means is the owner's decision.

**Solver iterations, measured again.** `Tools\sweep.ps1` on `stand_npc`, the body awake:

| Position iterations | Pelvis drift | Fastest body after 2 s | Assist force share | Floor's push over weight |
| --- | --- | --- | --- | --- |
| 1 | 3.75 cm | 0.60 rad/s | 0.088 | 1.11 |
| 2 | 3.59 cm | 0.47 rad/s | 0.100 | 1.08 |
| 4 | 1.57 cm | 0.32 rad/s | 0.051 | 1.04 |
| 8 (the engine's default) | 1.47 cm | 0.23 rad/s | 0.039 | 1.02 |
| 16 | 1.24 cm | 0.12 rad/s | 0.026 | 1.01 |
| 32 | 1.05 cm | 0.17 rad/s | 0.020 | 1.00 |
| 64 (the NPC's) | 0.97 cm | 0.15 rad/s | 0.0185 | 1.00 |

M2 step 1 recorded 0.54 rad/s and an assist share of 0.153 at 8 iterations against 0.07 and 0.044 at 64;
much of that gap was the sleep. The count stays at 64; 16 looks as steady and is a candidate for part 7.
`tune_live_solver` used to guard the live change of the count with a change from 64 to 8 and a lower bound
of 0.25 rad/s on the feet, which the awake body no longer reaches (0.22). It now changes to 2 and expects
0.3 (measured 0.47, the same as a run that has 2 from the start).

**The push matrix after both changes** (128 runs, 169 s): the same table of falls as in M2, run for run.
Pelvis movement of those that stood: 1.0 to 3.2 cm at 10 N*s, 1.8 to 7.9 cm at 30, 6.8 to 14.5 cm at 80.
Worst joint stretch 0.28 cm, fastest joint 39.6 rad/s.

**The three falls of the suite on the corrected body:**

| Scenario | Rest after the fall began | Energy gained | Deepest in the floor | Deepest in itself | Fall went |
| --- | --- | --- | --- | --- | --- |
| `hit_chest_200_falls` | 0.60 s | 0.12 J | 0.09 cm (the chest) | 0 | 104 cm straight back (270 degrees) |
| `push_thigh_200_falls` | 2.64 s | 0.25 J | 0.71 cm (the right hand) | 0.10 cm (right forearm and lower spine) | 125 cm to its right (177 degrees) |
| `effector_push_chest_falls` | 0.86 s | 0.00 J | 0.48 cm (the chest) | 0 | 89 cm straight back (270 degrees) |

The way a fall went is the path of the centre of mass over the floor from the moment the body was let go:
0 degrees is the character's left (+x), 90 forward, 180 its right, 270 backward. All three are now held to
3 s, 1 J and 2 cm by expectations. Two runs gain a lot and are not falls to be
judged by it: the character whose leg muscles are switched off gains 95 J when the falling state gives its
legs 100 N*m/rad back (`tune_live_override`), and the character hanging from one hand gains 18 J as it
swings (`hang_npc_hand`).

**What the balance sense shows of hits the body "stands" through** (corrected body; margins to the edge of
the support polygon, lowest value while upright):

| Scenario | Centre of mass | Capture point | Remark |
| --- | --- | --- | --- |
| `stand_npc` | 9.9 cm | 9.9 cm | |
| `hit_chest_10_stands` | 8.0 cm | 5.8 cm | |
| `strike_slap_torso`, `effector_push_chest` | 6.9 and 6.3 cm | 3.5 and 2.5 cm | |
| `strike_punch_head`, `strike_punch_leg` | 5.4 and 7.6 cm | 1.4 and 2.9 cm | |
| `strike_punch_torso` | 5.6 cm | 0.7 cm outside, for 4 frames | |
| `hit_head_30_stands` | 8.7 cm outside | 13.1 cm outside, for 21 frames | One foot off the floor for 34 frames |

The last two stand only because the assist's anchors pull the body back to where it was spawned. With the
capture point deciding (part 3), a 30 N*s blow to the head needs an answer of its own.

**One run in about 25 differed from the others.** In the suite run that was to become the end-of-part
baseline, `drop_passive` (the stock mannequin, passive) gave other hashes than in every run before and
after: its fastest body read 5.44309447322 m/s instead of 5.44309447335, and the trajectories then went
apart (rest after 4.68 s instead of 4.42 s). Nothing had changed for that scenario. Chaos gathers its
contact constraints from worker threads in the order they finish unless its determinism mode is on
(`p.Chaos.Solver.Deterministic`, off by default). The mode is now set in `Config/DefaultEngine.ini`, and
every scenario run has a check that it is on:

| Run | Result |
| --- | --- |
| Six scenarios with the mode on against the same six without it | The same hashes, the same step time |
| The whole suite with the mode on against the runs before | The same hashes in all 34 (`drop_passive` its usual ones) |
| 96 runs (six scenarios, 16 times each, 16 processes on 8 cores), mode off | Identical per scenario |
| The same 96 runs, mode on | Identical per scenario |

The fault did not show again in 96 runs without the mode, so these runs do not prove that the mode cures
it; they show that it costs nothing. The M0 statement "identical in every run" was true of the runs made
then and is a little too strong in general. A baseline comparison that names a scenario the change cannot
have touched is the signal to look for this again.

**Repeatability, on the corrected body** (the runs were made before the determinism mode was switched on;
the suite after it gave the same hashes for all 34):

| Scenario | Runs compared | Result |
| --- | --- | --- |
| `stand_npc` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `stand_npc_10min` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `hit_chest_200_falls` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `push_thigh_200_falls` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `effector_push_chest_falls` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `strike_punch_forearm` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `drop_npc` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `tune_live_solver` | 2 headless + 1 rendered | Identical hashes in all 3 runs |

The suite one scenario after another and the suite eight at a time gave the same hashes for all 34.

**The limits for the whole set** (34 scenarios, one at a time, 357 s):

| Limit | Worst scenario | Value |
| --- | --- | --- |
| Joint stretch of a simulated body at most 2 cm | `impulse_passive` | 0.86 cm |
| Mean physics step at most 1.5 ms | `effector_push_chest_falls` | 0.49 ms |

`Tools\bootstrap.ps1` takes 5.1 minutes (9.6 before the part): the suite 100 s, the push matrix 170 s, the
unit tests (26) 16 s, asset generation 11 s, the session's self-test (14 checks) 10 s. Build and suite
together are inside the 5 minutes of NFR-6 again.

**Thresholds chosen in this part,** from what was measured, accepted by the owner on 23 November 2026 with
one change (energy gain 1 J instead of the 5 J proposed; the suite passes with it and no hash moved): a
falling or lying body may gain 1 J (measured 0.25 J at most); bodies that may collide may overlap by 0.1 cm in the
spawn pose (measured 0); a foot stands from 20 N of the floor's push (it carries about 390 N standing, and
192 N in the worst frame of the sleeping body); a standing scenario keeps its capture point 5 cm inside
the support (measured 9.9 cm).

Pictures looked at: `stand_npc` with all overlays (the support polygon under both feet, the capture point's
ring inside it on the spot under the centre of mass, 387 and 397 N on the feet); `hit_chest_200_falls` at
3.05 s (the capture point 71 cm behind the body, red); `hit_head_30_stands` at 3.12 s (one foot carrying
842 N, the other none); the passive NPC after its drop, lying with its arms beside the body and not in it.

Not measured: balance on anything but the two feet (a body on hands and knees has no support polygon
yet); penetration of convex hulls (the NPC has none); falls against walls or furniture; the cost of the
never-sleeping body while it lies still (it is solved like a standing one, 0.45 ms); whether the
determinism mode holds for a scene with props.

### M3 part 2, step 0 (collarbones, inertia of the hands), 2026-11-23

The NPC's physics asset got a body for each collarbone (0.64 kg each, from the chest's 12.77 kg; joint
ranges in `Specs/README.md`), five more pairs under `collision.disable` (neck and each collarbone 6.0 cm
into each other in the spawn pose, the two collarbones, the chest and each upper arm) and three times the
inertia on the hands. 18 bodies, 17 joints, 127 colliding pairs.

Against `m3_part1_end`: 29 scenarios changed, every one with the NPC's asset in it; the five without it
(`drop_passive`, `drop_metahuman`, `impulse_passive`, `face_metahuman`, `portrait_metahuman`) did not.
`room_portrait`, `vr_room_look` and `vr_room_perf` hold the asset without simulating it: the body hash
moved because there are two more bodies, the bone hash did not. All 35 pass (one new scenario). The push
matrix has the same 128 outcomes; its fastest joint is 38.4 rad/s (39.6 before).

| Figure | Before | After |
| --- | --- | --- |
| Standing: assist force share | 0.0185 | 0.0171 |
| Standing: pelvis drift | 0.97 cm | 0.85 cm |
| Standing: fastest body after settling | 0.150 rad/s | 0.155 rad/s |
| Standing: capture margin (minimum) | 9.9 cm | 9.8 cm |
| `hit_chest_200_falls`: rest after the fall / energy gain | 0.60 s / 0.1 J | 0.62 s / 0.2 J |
| `push_thigh_200_falls` | 2.64 s / 0.2 J | 2.57 s / 0.1 J |
| `effector_push_chest_falls` | 0.86 s / 0.0 J | 0.86 s / 0.1 J |
| `strike_punch_forearm`: fastest joint | 10.2 rad/s (`upperarm_l`) | 20.9 rad/s (`hand_l`) |
| `hang_npc_hand`: limit violation / energy gain | 0.4 deg / 18 J | 4.4 deg / 5.6 J |

The collarbones alone failed `strike_punch_forearm`: the wrist snapped at 41 rad/s in the frame of the
punch and was at 7 in the next. It was not the collarbones' doing. The same punch on the old asset:

| Height of the punch | 102.6 cm | 104.6 | 106.6 (the scenario) | 108.6 | 110.6 |
| --- | --- | --- | --- | --- | --- |
| Old asset | 10.3 | 10.3 | 10.2 | 36.8 (`hand_l`) | 24.6 |
| Collarbones | 29.4 | 35.5 | 41.1 | 46.8 | 5.0 |
| Collarbones, hands at 3x inertia | 16.1 | not run alone | 20.9 | 23.5 | not run alone |

`strike_punch_forearm_heights` strikes 102.6, 104.6, 108.6 and 110.6 cm one after another on the final
asset: 22.8 rad/s at most.

What was ruled out: the hand touching thigh or pelvis (switching those pairs off left the hashes the same),
the collarbones' mass (0.004 to 0.016 of the body: 31 to 43 rad/s), and seven solver settings (soft limits
off, velocity projection, position solved last, shock propagation, inertia ratio, parent mass ratio, CCD:
40.4 to 41.5). Inertia conditioning did work: `p.Chaos.Solver.InertiaConditioning.Distance` 0 / 20 (default)
/ 40 / 80 gave 62 / 41 / 25 / 14 rad/s; but over the suite `push_thigh_200_falls` then took 3.3 to 6.7 s to
rest at any distance from 30 to 100, and `tune_live_solver` no longer told 2 iterations from 64 by its
bound. The hands' inertia, from the engine, is 5.5 / 15.2 / 12.6 kg*cm2, close to de Leva's 5.9 / 14.4 / 9.6
for a hand of 0.49 kg; so the factor of 3 is a stability measure, and is written down as one. Factors of 2,
3 and 5 gave 36, 24 and 16 rad/s at the worst height; standing and the three falls were within noise of
each other at all of them, except that `push_thigh_200_falls` rested in 3.5 s at a factor of 2: its rest
time is itself on an edge (a lying body that rolls over late), which part 5 has to deal with.

`series.csv` has two more columns (the fastest joint of each frame and its speed), and `metrics.json` the
inertia of every body. Both are measurements only.

Missed in step 0 and found in step 1: the session's self-test aimed its drag handle at a point written down
for the old asset, and with the collarbones the arm hangs elsewhere, so the handle took nothing (12 of 14
checks). Measured on `stand_npc` with `body.bodies[].final_location_cm`: the upper arm's centre of mass is
1.8 cm lower and 1.1 cm closer to the body than before, the hand 3.4 cm lower and 3.3 cm closer; the
collarbone gives a little under the arm's weight. (The chest's centre of mass is 3 cm lower only because the
collarbones' shapes left it.) The test now aims at where the forearm is: 14 of 14.

### M3 part 2, steps 1 to 3 (animation instance, poses from text, flinch offset), 2026-11-23

Step 1, the native animation instance giving the reference pose: all 35 scenarios had the same hashes as
`m3_part2_step0`. Step 2, poses from `Config/Tuning/poses.json` with a stance of zeros: the same 35 again,
plus `pose_lean`, where the stance changes at 3 s (chest -10 and -8, neck +12, collarbones +10, elbows +30):
the forearm's target ends 30.0 degrees from the reference pose, which is the angle asked for, and the body
holds the new pose with a mean error of 1.16 degrees (1.08 standing in the reference pose; the worst single
muscle is a collarbone, 4.7 to 5.8 degrees). Pictures before and after: the chest leans forward, the head
stays level, the elbows close.

Step 3, the flinch's pose offset: 15 scenarios changed, exactly those in which a hit makes the body flinch;
the 20 others kept their hashes. The first version dropped the offset when the Flinch state ended, and two
falls gained 13.8 and 7.9 J (over the 1 J bound) in the frames after they began to fall; fading it by its
own clock instead gave 0.13, 0.00 and 0.06 J for the three falls, which rest after 0.61, 2.43 and 0.86 s.

| Figure | Step 0 | Step 3 |
| --- | --- | --- |
| `hit_head_30_stands`: fastest joint / assist / capture margin (min) | 12.3 rad/s / 0.0359 / -13.2 cm | 18.5 / 0.0365 / -13.3 |
| `hit_chest_10_stands`: fastest joint | 1.3 rad/s | 6.1 |
| `strike_punch_torso`: fastest joint | 3.7 rad/s | 9.2 |
| `effector_push_chest`: fastest joint / capture margin | 1.4 rad/s / 2.6 cm | 3.4 / 2.7 |
| `hit_chest_200_falls`: energy gain / rest | 0.2 J / 0.62 s | 0.13 J / 0.61 s |
| `push_thigh_200_falls` | 0.1 J / 2.57 s | 0.00 J / 2.43 s |

Step 4, two-bone leg placement (`pose_foot_place`: pelvis welded to the world, foot anchors of the assist
off, the left foot asked to go 12 cm up and 10 cm forward): the foot ends 1.8 cm from its point, the knee's
target is bent 61 degrees, the body holds the pose with a mean error of 1.08 degrees. Three things went wrong
on the way. With the pelvis on a ball joint instead of welded, the lifted leg tipped the body over the pin,
the world-fixed point moved against the body, the leg chased it and the body fell (35 degrees of tilt): the
test, not the placement. With the placement switched on at full weight in one step, the knee's target
jumped 61 degrees and the calf snapped at 84 rad/s; eased in over `poses.blend_s`, 4.7 rad/s. My first
guess for the fall was a knee folding backward; the knee's bending direction is now taken as the front of
the mesh rather than from the pose, which changed nothing in this test (same hashes) and is kept as the
safer rule. The other 36 scenarios kept their hashes.

The push matrix differs in one of 128 runs: 80 N*s backward on the chest no longer fells the body (it did
in step 0). Under the M2 rules the matrix still passes; under the owner's M3 table, standing without a step
at 80 N*s is a failure, which parts 3 and 4 have to deal with.

### M3 part 3 (standing balance), 2026-11-23

Same machine and settings. Four steps, each against its own baseline (`m3_part3_start`, `_step2`, `_step3`,
`_end`). The push matrix now has five bodies (the pelvis added) and names an outcome per push.

**Step 1, measurements only: all 37 scenarios and the 128 runs of the old matrix kept their hashes and
outcomes.** What the assist holds in quiet standing (`control.assist_load.by_body`), as a share of body weight:

| Body | Horizontal | Vertical |
| --- | --- | --- |
| Pelvis | 0.0135 | (no vertical spring) |
| Feet together | 0.0031 | 0.0015 |
| Chest, head | 0 | 0 |

So 79% of the 0.0171 was the pelvis standing about 0.7 cm from its anchor.

**Step 2, the capture point decides and the anchors follow the feet.** Changed exactly the scenarios in which
a foot lifts or the body falls; the 33 standing ones kept their hashes. Three things had to be corrected on
the way, each found by a run:

| Tried | What happened | Now |
| --- | --- | --- |
| Step reach compared with the capture point's margin now (30 cm) | `push_thigh_200_falls` (2000 N for 0.1 s on a thigh) was held at -29 cm by the assist with 110% of body weight, and did not fall | The margin is grown by e^(omega T) over a step (T = 0.25 s, 2.2 times) and compared with 50 cm: lost at about 23 cm outside |
| Each decision held for 0.05 s | A 200 N*s blow to a thigh put the capture point 38 cm out in its first frame; the assist (3 times body weight) brought it back in three frames, and the body "stood" | Lost is decided at once; only "a step is needed" waits (`decision_s`, 0.03 s) |
| Support = the feet carrying 20 N or more | After a 30 N*s blow to the side of the chest the far foot carried 14 to 30 N for 0.15 s, the polygon shrank to one foot, and a step "was needed" | Support for decisions = every foot the floor touches, or touched within `lift_grace_s` (0.25 s, for a foot knocked off by a blow to the leg) |

Before any strategy, the matrix with the new decision: 30 N*s 27 stood, 13 held (4 of them failures, the
thighs sideways), 200 N*s 38 fell and 2 held (diagonal blows to a thigh, before lost was made immediate).

**Step 3, ankle and hip strategy.** Sweeps on `stand_npc` and `hit_chest_10_stands`
(`Saved\Sweeps\ankle1..3`):

| Ankle gain | Slow part | Assist share, standing | Pelvis drift |
| --- | --- | --- | --- |
| 0 | none | 0.0171 | 0.85 cm |
| 1 | none | 0.0147 | 0.66 cm |
| 2 | none | 0.0131 | 0.55 cm |
| 0 | on the capture point, 2 s | 0.0111 | 0.80 cm |
| 1 | on the pelvis's offset from its anchor, 0.5 s | 0.0050 | 0.59 cm |
| 2 | the same | 0.0052 | 0.50 cm |

The slow part on the capture point left 0.007 on the pelvis spring: under its weight the posture sags so that
the pelvis stands 0.35 cm off its anchor while the centre of mass is where it was. On the pelvis's own offset
it takes that up. Chosen: gain 1, slow part 0.5 s.

Sideways the ankles made things worse. A 30 N*s push to the side of the chest: capture margin -5.1 cm with no
strategy, -9.3 with ankle gain 1, -9.9 with 3; turned the other way, -10.4. Leaning the legs as a parallelogram
(ankles and hips turned together) lifted the unloaded foot; the ankles alone did the same. `ankle_side_gain`
is 0. The hip strategy (trunk toward the capture point while it is outside the support) at 3 degrees per cm
changed no outcome of the 30 N*s row and lowered the assist's peak on a backward push to the chest from 0.238
to 0.179 of body weight; it is on at 3, forward and back only.

**Step 4, a held push.** Changed the two scenarios in which a hand presses longer than a blow
(`effector_push_chest`, `effector_push_chest_falls`, which now flinches once instead of twice); the strikes
kept their hashes, and the push matrix kept all 160 outcomes. Two more corrections:

| Tried | What happened | Now |
| --- | --- | --- |
| The lean moved the strategies' reference and the anchors only | Under 40 N the assist carried 4.7% of body weight: the push. The ankle muscles held the upright pose against the lean | The ankle targets tilt by the lean's angle (lean / centre-of-mass height) |
| The legs' turns weighted by each leg's share of the load | With the larger turns of a lean, the body rocked from foot to foot (each foot at 0 N in turn); assist up to 0.43 | Both legs turn alike |
| The held-push window equal to the hit's pressure time (0.1 s) | A 2000 N shove of 0.1 s counted as held | `held_push_s` 0.3 s |

| Scenario | Flinches | Lean | Step needed | Assist, mean / max |
| --- | --- | --- | --- | --- |
| `push_hold_chest`: 40 N backward on the chest for 2 s | 0 (the onset is gentle) | 6.2 cm (6.1 by F h / m g) | no | 0.017 / 0.143; 0.006 to 0.008 during the hold |
| `effector_hold_chest`: a hand pressed against the chest for 3 s | 1, as it lands | 10 cm (the limit) | 1.8 s of it (117 N: more than a lean can take) | 0.032 / 0.193 |

A hand held at a fixed point presses harder the more the body leans into it (the chest comes toward the hand):
moving the hand's target 4 cm further out only lowered its force from 138 to 104 N. The clean test of a lean
without a step is the scripted force.

**The body at the end of the part, against the start:**

| Figure | Start | End | Bound |
| --- | --- | --- | --- |
| Assist force share, `stand_npc` (10 s) | 0.0171 | 0.0052 | 0.0093 |
| The same, `stand_npc_10min` | 0.0171 | 0.0041 | 0.0093 |
| Pelvis drift, standing | 0.85 cm | 0.59 cm | 5 cm |
| Fastest body after settling, standing | 0.155 rad/s | 0.162 rad/s | 0.3 rad/s |
| Capture margin, standing (minimum) | 9.8 cm | 9.0 cm | 5 cm |
| `hit_head_30_stands`: assist mean, capture margin min | 0.0365, -13.3 cm | 0.0270, -14.5 cm (step needed 9 frames) | |
| `hit_chest_200_falls`: rest after the fall, energy | 0.61 s, 0.13 J | 0.79 s, 0.23 J | 3 s, 1 J |
| `effector_push_chest_falls` | 0.86 s, 0.06 J | 0.92 s, 0.00 J | 3 s, 1 J |
| `push_thigh_200_falls` | 2.43 s, 0.00 J | 3.69 s, 3.22 J | 4 s, 4 J until part 5 |

`push_thigh_200_falls`: the fall is decided 0.03 s earlier, so the rest is counted from earlier; and 2 s into
the fall the rolling body puts a foot 0.99 cm into the floor, and being pushed out gives it 3 J in one frame.
Its trajectory is chaotic: it gained 0.005, 0.008 and 3.2 J in three successive builds that changed nothing
of the fall itself. Its limits were loosened on the owner's decision until part 5.

**Push matrix at the end** (160 runs, 212 s):

| Impulse | Stood | Held | Stepped | Fell | Verdict |
| --- | --- | --- | --- | --- | --- |
| 10 N*s | 40 | 0 | 0 | 0 | judged: pass |
| 30 N*s | 37 | 3 (head at 0, 180, 270 degrees) | 0 | 0 | judged: pass, 3 pending part 4 |
| 80 N*s | 12 | 24 | 0 | 4 (head at 0, 180, 270; chest at 270) | reported only: 12 fail (pelvis at 45 and 135, thighs at most angles), 24 pending |
| 200 N*s | 0 | 0 | 0 | 40 | judged: pass |

All 44 falls were decided by the capture point. After a 30 N*s push the assist's commanded force peaks at 0.26
of body weight (median of the row; 0.53 at most), against 0.009 in quiet standing.

Repeatability: `push_hold_chest`, `effector_hold_chest` and `hit_head_30_stands` rendered with all overlays
gave the hashes of their headless runs. `Tools\bootstrap.ps1`: all 8 steps pass (40 scenarios, 27 unit
tests, the matrix, the session's self-test 14 of 14).

Pictures looked at: `push_hold_chest` at 4.8 s (leaning 6.2 cm into 40 N, ankles turned 4.7 degrees, assist 0.8%, the feet
at 389 and 396 N), `effector_hold_chest`
at 5.0 s (the trunk bent toward the hand, 117 N, "needs a step"), `hit_head_30_stands` 0.15 s after the blow.

### M3 part 4 (stagger and steps) and the audit of the body's movement, 2026-11-24

**Runs share engine processes.** A run was 14 s of engine start-up for 1 s of physics. A job queue
(`-ScenarioQueue`, `UScenarioBatchSubsystem`) now lets one process run many scenarios, each in a fresh world.
The first scenario a process runs differs bit for bit from the same scenario run later in it (35 of 48 suite
scenarios had other hashes than in separate processes), and later runs do not depend on what ran before them
(three different chains gave the same hashes). So every process runs `Tools\batch_warmup.json` first, and a
single run is a queue of one job: with that, 48 of 48 scenarios have the same hashes run alone, eight processes
at a time and twelve. Suite 110 s to 25 s (80 s with `-Long`), push matrix 213 s to 47 s.

**Part 4, figures before the audit** (baseline `m3_part4_s4_end`): suite 47 of 48 (`effector_push_chest_falls`
stood, backing away from the hand), push matrix 159 of 160, 80 N*s row 38 stepped and 2 fell, steps per push
median 1 and at most 4, no joint over 30 rad/s outside the 200 N*s row, energy gain at most 1.2 J, quiet-standing
assist share 0.0051.

**The audit** looked at the motion itself, on sheets of pictures over time (`Tools\film.ps1`), since every
check passed while the body stood with its arms out, fell like a plank and stayed in a lunge. Measured on the way:

| What | Before | After |
| --- | --- | --- |
| 80 N*s on the chest, backward | fell at 3.03 s without a step ("61 cm by the time a step lands: beyond a step") | two steps, stands |
| 120 N*s on the chest, backward | fell | three steps, stands |
| 200 N*s on the chest, backward | plank, lies straight | one stumble, sits down and curls, lands on its back |
| 200 N*s row of the matrix | 40 fell | 32 fell, 8 stepped (pelvis and thighs, 3 to 15 steps) |
| Rest after a fall (back, forward, side) | 2.6 s (back) | 1.7, 2.6, 1.6 s; 3.5, 3.9, 3.3 s while "lying" still waited for a motionless body |
| Energy gain in a fall | 0 to 1.2 J | 28, 21, 11 J: the work of folding the legs and putting the arms out |
| Collarbone under a soft arm (40 N*m/rad) | | sagged 10.2 degrees in the arms' group; pose error mean 2.0 to 1.0 degrees once in the trunk's |
| Quiet-standing assist share (10 s, 10 min) | 0.0051, 0.0041 | 0.0048, 0.0038 |
| A punch on the forearm (8.5 m/s) | moved it 10 cm (arm held out) | 1.8 cm: the arm hangs at the hip and is pressed against it |

After the audit (baseline `m3_audit_end`): suite 50 of 50 with `-Long`, push matrix 159 of 160 (the same cell,
`push_neck_01_090_030`, two steps where one is allowed), unit tests 28, session self-test 14 of 14. Against
`m3_part4_s4_end` 36 scenarios have other hashes: every one with an actively controlled body, as the stance, the
arms and the neck changed for all of them; the 10 with a passive body or none are bit for bit as they were.
Renamed: `effector_push_chest_falls` to `effector_push_chest_steps`, `push_thigh_200_falls` to
`push_thigh_200_stumbles`; new: `hit_chest_200_forward_falls`, `hit_chest_200_side_falls`.

Seen and not solved in the audit itself (solved the same day, below): `hang_npc_hand` (a body hanging by one
hand) took the pose of a fall backward and swung for the whole run, 119 J of muscle work where there were 5.5:
the fall's pose asked only whether the posture gave way, not whether the floor was coming. `hit_head_30_stands` sits on an edge: the same blow needs a
step or not (capture margin -8.7 or -4.9 cm) with the arms 3 degrees further in or out; its check is now "stands,
one step at most".

### The regrouping step, 2026-11-24

After recovery steps the body stood wherever they had left its feet: 25 to 60 cm from its stance, in a lunge.
A step that brings a foot back beside the other was written in the audit and left off, because out of a lunge
it ended in another loss of balance. What stood in its way, each measured:

| What | Seen | Now |
| --- | --- | --- |
| The ankle strategy holds the capture point over the middle of the feet | of a weight shift of 19 cm forward the pelvis went 5 | a shift moves the strategy's reference with it |
| The foot left behind lowers the pelvis so that its leg can reach | pelvis 15 cm down although that foot carried 0 N; centre of mass over the stance heel, 17 cm short, for 2 s | a foot about to lift lowers the pelvis by the share of the weight it carries: the body rises on the stance leg |
| The foot lifted once the centre of mass stood inside the stance sole | 0.5 to 1.2 s of shift with the pelvis's goal up to three times too far; the body went on past the foot, or the legs folded (pelvis 35 cm down, "falling") | lifts when the capture point at landing is 5 cm inside the support to come; the goal is led by the distance the capture point still has to go, 25 cm at most |
| What was left of the shift after landing | the anchors glide from where the body is, so the shift counted twice: a 54 cm step to the side to catch it | dropped as the foot lands |
| A foot on its toes has a sole polygon of its toes only | target sole and stance middle 12 cm too far forward | the whole sole is taken for both |

With it (`balance.regroup_wait_s` 0.6): weight shift 0.15 to 0.2 s out of a short stance, 0.7 to 1.3 s out of a
lunge of 50 to 60 cm; the feet end 0.7 to 5.0 cm from their stance (`control.stance_error_cm`, checked by
`max_stance_error_cm` 8 in the five scenarios that step). In the push matrix 48 of 160 pushes end with a
regrouping step: none gives up, none falls, one (`push_thigh_l_045_200`) needs one more step for its balance.
The matrix's outcomes are as before (159 of 160, the same cell); it does not count regrouping steps as steps the
push cost, and neither does `max_recovery_steps`.

Suite 50 of 50 with `-Long`, unit tests 28, session self-test 14 of 14, assist share 0.0048 and 0.0038. Against
`m3_audit_end` 11 scenarios have other hashes: the five that make recovery steps (they now regroup), and the six
asked steps (`step_*`), whose weight shift now moves the ankle strategy's reference (landing errors, lift times
and joint speeds as they were to the digit shown: 1.1, 1.6, 2.0 cm; 3.41 s). The other 39 are bit for bit as they
were. New baseline: `m3_regroup`.

### A body held up does not fall, 2026-11-24

`hang_npc_hand` (pinned by the right hand, dropped 60 cm onto that arm) took the pose of a fall backward, knees
to the chest, and kept it for the whole run: 119 J of muscle work. A body that is not upright, has touched
nothing of the world for 0.4 s and whose centre of mass is not 5 cm below the lowest it has been is now held up
(`thresholds.held_*`): it lets go of the fall's pose and hangs straight. Held up at 0.74 s, 34 J (the curl of the
first 0.4 s, before the body can know), checked by `max_energy_gain_j` 60. It still swings for the whole run:
that is the drop onto one arm, not the muscles. In the 160 runs of the push matrix and the other 49 scenarios
nothing is ever taken for held up: their hashes are those of `m3_regroup`.

### The last cell of the push matrix: a body on its toes, 2026-11-24

`push_neck_01_090_030` (30 N*s on the back of the head) made two steps where the table allows one, since part 4.
On the pictures the body bows its head, rises on its toes, and then steps *back* twice, away from the way it was
pushed. Rising on the toes is the ankle strategy doing its work (the centre of pressure goes forward to the
capture point); with the heels 1.5 cm off the floor the support polygon was the toes alone, and the capture
point, under the raised heels, was "4.7 cm outside the support" behind it. A decision now counts the whole sole
of every foot that touches the floor. The cell stands without a step, and the matrix passes for the first time:
160 of 160 (10 N*s: 40 stood; 30: 36 stood, 4 one step; 80: 2 stood, 38 stepped; 200: 32 fell, 8 stepped), 42
regrouping steps, none given up, none in a fall.

Against `m3_regroup` 11 more scenarios have other hashes: those in which a foot is tilted at some time while the
capture point is near the edge of what touches (`effector_hold_chest`, `effector_push_chest_steps`,
`push_hold_chest`, `push_thigh_200_stumbles`, `stand_foot_moved`, `step_forward` and its mirror,
`strike_punch_head`, `strike_punch_torso`, `tune_live_file`, `tune_live_override`); all pass. The other 38
(quiet standing, the light hits, the falls, the passive bodies) are bit for bit as they were. New baseline:
`m3_audit2_end`.

One suite run in the course of this gave `drop_npc` other hashes (1200d5db instead of c8bde50f); alone and in
three later suite runs it gave the baseline's. Not explained; the determinism mode was on. If a baseline
comparison names a scenario a change cannot have touched, run it again, and count how often this happens.
One push-matrix run gave no result for one of its 160 runs (`push_spine_04_000_080`, no output folder at all);
the same matrix run again was complete. Both happened in the first queue run after a build, which is the one
thing they share; not looked into further. (Both were found and fixed later the same day: "Runs that did not
repeat, and jobs nobody ran" below. The build had nothing to do with either.)

### The idle and the arms, 2026-11-24

Two things of the audit's list of what still looked unnatural: a standing body was a statue (0.3 to 0.5 cm of
travel per body in ten seconds, the solver's own noise), and its arms hung while it fought for its balance.

**The idle** is the motion of the engine's `MM_Idle` (a loop of 7.57 s, 228 keys), as planned for part 7. The
clip was measured first (a script in the editor): spine 0.5 to 0.9 degrees per bone, neck and head 2,
collarbones 3 to 5, upper arms 2, the pelvis travels 0.8 cm, the head 2.5, the hands 2 to 6; the feet stand.
The bones of the mannequin and of the MetaHuman body have the same axes (their reference poses agree to about
0.01 in every quaternion component, hands aside), so what a bone does in the clip relative to the clip's first
frame is added to the same bone of the body as it is, with no retargeting; the clip's own pose (one foot
forward) and its legs are not used. The pelvis's travel leans the whole body about its ankles, by the way a
lean into a held push goes.

| `stand_npc`, after 2 s of settling | without | with the idle |
| --- | --- | --- |
| Travel of the pelvis, cm | 0.3 | 1.3 |
| Travel of the chest, cm | 0.4 | 2.1 |
| Travel of the head, cm | 0.5 | 4.1 |
| Travel of the hands, cm | 0.3 and 0.3 | 6.6 and 4.1 |
| Travel of a foot, cm | 0.0 | 0.0 |
| Fastest body, m/s | 0.008 | 0.083 (a hand) |
| Fastest turning body, rad/s | 0.146 | 0.193 |
| Assist share, 10 s | 0.0048 | 0.0067 |
| Assist share, 10 min | 0.0038 | 0.0066 |

The assist's share rises by the sway (0.0052 with the turns alone): its anchors move and the body follows them.
It stays under the bound of 0.0093. With both weights 0 the six scenarios tried gave the baseline's hashes bit
for bit.

**The arms** take one of three poses on top of the stance (`poses.balance_*`), in proportion to how far the
capture point is from where the body stands (nothing under 4 cm, in full at 30, with a lag of 0.12 s). In
`stand_npc` they do nothing: its hashes are the same with and without them. In the films a push backward brings
the arms forward with bent elbows by 0.2 s and they are down again when the feet are back together; a push
sideways spreads them, the far one higher; an asked step leaves them down; a body knocked over backward
throws them forward as it goes.

| Push matrix | before | with the idle | with the idle and the arms |
| --- | --- | --- | --- |
| Verdict | 160 of 160 | 160 of 160 | 160 of 160 |
| Steps at 80 N*s (40 pushes), sum and most | 87, 7 | 88, 9 | 81, 6 |
| Stepped and fell at 200 N*s | 8 and 32 | 10 and 30 | 8 and 32 |
| Steps of those that stood at 200 N*s, sum and most | 62, 11 | 84, 13 | 59, 13 |
| Mirror pairs that end differently | 2 | 3 | 3 |

Thirteen pushes ended differently with the idle than before (all among the outcomes the table allows): the body
is at another phase of its sway when the push comes, and the cells near an edge go either way.

Against `m3_audit2_end` all 40 scenarios with a controlled body have other hashes, as they must with a body
that moves from its first second; the 10 without one are unchanged. Three limits were moved, each for the
reason given in `CLAUDE.md`: `max_settled_speed_mps` of the two standing scenarios from 0.05 to 0.15, and
`min_pose_offset_deg` of `pose_lean` from 29.9 to 27. New check `min_settled_head_travel_cm` (2 cm) in the two
standing scenarios; the suite passes 50 of 50 with `-Long`. Baseline `m3_audit3_end`.

Seen and left: of the 8 pushes of 200 N*s the body stands through, 7 take more than four steps (up to 13), as
before; a person would have fallen or stopped sooner.

(The figures of this section were taken before the solver was kept in step, below. With it the matrix is 160 of
160 with 81 steps at 80 N*s, 6 at most, and 9 stepped and 31 fell at 200 N*s; the standing figures are the same.)

### Runs that did not repeat, and jobs nobody ran, 2026-11-24

Checking the baseline of the section above, the suite named `tune_live_file` as changed in two runs of five, with
no change made. Two defects of the tooling were behind what had been written off as one-off flakes.

**A body that falls came out differently on a busy machine.** Measured by running one scenario 160 times with
16 engine processes side by side (in one process, one run after another, 47 of 47 were the same):

| Scenario | Runs of 160 that differed, before | after |
| --- | --- | --- |
| `drop_npc` | 29 (six other hashes) | 0 |
| `tune_live_file` | 5 to 15 (three other hashes) | 0 |
| `hit_chest_200_falls` | 4 | 0 |
| `tune_live_override` | 3 | not run |
| `drop_passive` | not run | 0 |

The other hashes came back again and again, so this was no noise in the last digit but a choice between a few
ways for a run to go. Each run that differed was the same as the usual one, to the printed digit, until a body
met the floor or slid off another (5.41, 5.49 or 6.87 s in `tune_live_file`), and from there a contact came a
frame earlier or later. Ruled out by measurement: the scenario run before it in the same process (47 of 47 the
same), the millisecond budget of the tree's build (`p.aabbtree.UseTimeSliceMillisecondBudget=0`: 14 of 320
differed), the threads of collision detection (13 of 319), all of the physics on one thread (17 of 320). What it
was: Chaos rebuilds the structure it finds colliding pairs with in a background task, and every step takes the
new one in if the task is done by then and carries on with the old one if not
(`FPBDRigidsEvolutionBase::ComputeIntermediateSpatialAcceleration`). The boxes of the moving bodies in it are
padded from when each was last put in, so the two structures do not give the same pairs, and a pair found a
step later is a contact a step later. The solver's determinism mode sorts constraints; it does not touch this.
Waiting for the task before each step alone made it worse (a third hash in 28 runs of 160: the task started by
the wait is itself done or not when the step looks). `FScenarioStage::KeepSolverInStep` waits for the rebuild
before every substep, twice, and lets none run during the step: 640 runs of four scenarios, no two different.
It costs nothing that shows (mean physics step of `stand_npc` 0.742 ms before, 0.739 after). It changes every
run's hashes, the passive bodies' too, since every step now uses a freshly built structure; the suite passes
50 of 50 and the push matrix 160 of 160 with it, two suite runs and a matrix run repeat the baseline in full.
Baseline `m3_audit3_end` was taken again with it.

**A job of the queue was sometimes run by nobody.** One job in about a thousand ended without a result when 16
processes shared a queue (twice in 1900 runs; once in the push matrix the day before, with 8). A process took a
job by moving its file, on the understanding that only one move can succeed. On Windows a move opens the file
and renames it through the handle; two processes can each open it before either renames, and then both renames
succeed, the second taking the file from under the first. Each then failed to read "its" file (gone, or still
held by the other), logged an error nobody read, and went on to the next job. The logs showed it once the queue
kept them: `Cannot read file '...taken/00099.json.10740'` in one process and the same job under another number
in a second. A job is now taken by writing a marker while a lock file is held open for writing, which one
process at a time can do (`UScenarioBatchSubsystem::TakeJob`): 1600 jobs with 16 processes, none lost. And a job
that ends without a result is no longer silent: `Invoke-ScenarioQueue` prints `QUEUE WARNING` and keeps the
processes' logs.

### M3 part 5 (the fall), 2026-11-24

What the audit had left of the part: knockdowns from 8 directions, the muscles' torque caps, the softness of
the joint limits, and continuous collision detection decided by measurement. `Tools\knockdowns.ps1` knocks the
standing body down with a blow of 200 N*s on the chest from each of 8 directions and judges every fall: it
falls, rests within 3 s of starting to, gains no more than 40 J (its muscles' own work), goes no deeper than
2 cm into the floor or into itself, and no joint stretches by 2 cm.

| 8 knockdowns, chest, 200 N*s | before the part | after it |
| --- | --- | --- |
| Pass | 8 | 8 |
| Rest after the fall began, s | 1.26 to 2.78 | 1.20 to 1.94 |
| Energy gained, J | 5.0 to 25.7 | 5.4 to 25.9 |
| Deepest in the floor, cm | 0.65 | 1.22 |
| Deepest in itself, cm | 0.28 | 0.42 |
| A joint past its range, degrees | up to 2.5 | up to 3.4 |

With the head as well and 300 N*s (32 falls) 29 passed before and 31 after; the one left is a head knocked
back with 300 N*s that rests after 3.89 s. Those blows are beyond anything a hand gives (200 N*s on the head
turns the body over in the air) and are run to see what breaks, not judged.

**A figure that was wrong since M2.** `max_joint_limit_violation_deg` compared the engine's
`GetCurrentSwing1`, `GetCurrentSwing2` and `GetCurrentTwist` with the limits. Those are not the angles the
solver limits (each is the twist of the whole turn about one axis): a limp wrist read 40 degrees outside its
range, a relaxed hip 23, and making every limit hard changed none of it, which is what gave it away. Reading
the engine then showed a second error of the project's own: the solver does not limit the two swings together
as an ellipse but each by itself (`FPBDJointCachedSolver::InitPyramidSwingConstraint`; the linear solver is
the default for every joint). Measured as the solver limits a joint (`FSandboxPoseRig::LimitExcessDeg`), the
same runs read:

| Furthest a joint got past its range, degrees | old figure | as the solver measures |
| --- | --- | --- |
| `drop_npc` (a passive body dropped) | 40.6 | 4.2 |
| 16 knockdowns | 13.6 | 2.5 |
| `push_thigh_200_stumbles` (steps) | 8.5 | 5.8 |
| `stand_npc` | 0.0 | 0.0 |

**Joint limits.** Every limit of the generated asset was the stock asset's soft one (stiffness 50, damping 5),
copied from the template in silence. It is now written in the spec per joint (`"limit"`). With the spec saying
what the template had, the suite's 49 scenarios came out bit for bit as before. All hard instead: joints no
better held (1.5 degrees against 2.5 in the knockdowns, 5.2 against 4.6 at the ankle in a step), and the falls
worse: 46 J gained in one against at most 26, rest after 4.0 s against 3.6, `hit_chest_200_falls` failing. The
soft limits stay.

**After the impact the muscles let go.** Seen on the sheets of pictures, not in a figure: a body knocked over
by the head went over in the air, landed on its head and shoulders and stood there, legs up, from 3.5 to 4.5 s,
held in the shape of its fall's pose; a body on its back kept its forearms in the air. The fall's pose was kept,
with the falling state's muscles, until the body was at rest. Now the muscles go over to the lying state's
`thresholds.impact_let_go_s` (0.5 s) after the trunk or the head first meets the floor. The body on its head
folds by 4.1 s and lies sprawled at 5.0; the one on its back lies flat with its arms on the floor by 5.5 s.
The value was chosen by the pictures. By the rest time it could not be: of the 32 falls, 0 to 4 take more than
3 s at every value tried from 0.15 to 1.0 s, a different few each time (0.45 and 0.5: none; 0.4 and 0.55: two
and three). What takes the time is a limb or the whole body rolling over slowly from where it came to lie, at
0.2 to 0.35 rad/s as a whole against the runner's 0.2: a body balanced on its side. Softer or more damped lying
muscles (legs' damping 0.3 to 2 s, all stiffness near 0) moved which falls were slow, not how many.

**Torque caps.** `muscles.*.max_torque_nm` were 0, no cap. They are now 250 N*m for the legs, 300 for the
spine, 80 for the arms and 50 for the neck, from published peak torques of an adult man (knee and hip
extension, trunk extension, shoulder and elbow, neck extension), one figure a group. Push matrix with them:
160 of 160; at 80 N*s 36 stepped, 2 stood and 2 fell (37, 3 and 0 without), 72 steps in all against 81; at
200 N*s 12 stepped and 28 fell (9 and 31). `effector_hold_chest` now flinches twice: the body steps back from
the hand, the hand meets it again on the collarbone with 5.3 N*s against a threshold of 5. A second touch is a
second flinch; its limit went from 1 to 2 (the defect it guards against was a flinch every 0.35 s).

**Continuous collision detection** stays off for the body. The owner's rule switches it on where a knockdown
sinks more than 2 cm into the floor; in 32 knockdowns the deepest was 1.5 cm. A passive body dropped from
1.5 m (`drop_npc`) does sink 8.3 cm and is thrown back out with 149 J, and that is not what the sweep cures:

| `drop_npc` | Deepest in the floor, cm | Energy gained, J |
| --- | --- | --- |
| As it is (8 solver iterations, no sweep) | 8.31 | 149 |
| Feet swept | 8.31 | 144 |
| Feet and head swept | 8.31 | 144 |
| 32 iterations | 3.18 | 13 |
| 64 iterations (what a body under control has) | 1.49 | 119 |

The spec can switch the sweep on per body (`"ccd"`); no body has it.

Against `m3_audit3_end`, 12 scenarios have other hashes, the ones in which a muscle asks for more than its cap
or a body falls: the three `hit_chest_200_*`, `tune_live_file`, `tune_live_override`, `hang_npc_hand`, the
four with recovery steps, `effector_hold_chest` and `pose_foot_place`. The other 38 are bit for bit as they
were. Suite 50 of 50 with `-Long`, push matrix 160 of 160, knockdowns 8 of 8, unit tests 28, session self-test
14 of 14; a second suite run and a second matrix run repeat the first in full. Baseline `m3_part5`.

**The look at Epic's Game Animation Sample** (downloaded by the owner; read by a script in the editor on the
sample's own project, 24 s, nothing rendered). Its ragdoll is a kinematic character that goes limp when hit and
is played back up by a montage; the logic is Blueprint and has no balance in it. Its clips, 34 sequences under
`Characters/UEFN_Mannequin/Animations/Ragdoll` on a skeleton with the mannequin's bone names:

| Clips | What they are | Length |
| --- | --- | --- |
| `M_ragdoll_getup_stand_B`, `_F`, `_L`, `_R` | From lying on the back, the front, a side to standing, in place (pelvis from 12 to 16 cm up to 92 cm, no travel) | 5.0 s |
| `M_ragdoll_getup_crouch_*`, `_walk_*`, `_run_*` | The same into a crouch, a walk or a run; they travel 5 to 8.7 m | 3.0 to 4.5 s |
| `M_ragdoll_getup_roll_L`, `_R` | Up out of a roll | 3.7 s |
| `M_ragdoll_poses_OnGround`, `_rolling`, `_injured` | Poses for a body on the floor, by the way it lies; seven injured states | a few frames each |
| `M_ragdoll_flail_armsFirst`, `_legsFirst`, `_armsDown` | Loops for a body in the air, chosen by the way it flies | 4.7 to 8.3 s |
| `M_ragdoll_reach_*` (nine) | Reaching for the coming impact, aimed by a blend space over direction and pitch | 2.0 s |

How its poses look on this body next to the three fall poses written here is in "M3 part 6, step 1", below.

### M3 part 6, step 1 (a recorded clip as the source of the target pose), 2026-11-24

The get-up takes the four clips of Epic's Game Animation Sample that end standing in place
(`M_ragdoll_getup_stand_B`, `_F`, `_L`, `_R`; owner's decision). They are made for the sample's mannequin, whose
bones have the names of the body's and a different reference pose. This step makes a clip a source of the
muscles' targets and shows the clips on the body; the body does not get up yet.

**How far the two reference poses are apart**, measured when a clip is read (`control.clips` of a run): the angle
between a bone's orientation in the reference pose of the clip's skeleton and of the body's mesh, in the space of
the whole body, and, for the segments of arms and legs, the angle between the ways the two point.

| Joint | Orientations apart, degrees | Directions apart, degrees |
| --- | --- | --- |
| `spine_02`, `spine_04` | 10.3, 7.6 | |
| `neck_01` (the head's bone) | 0.5 | |
| collarbone | 4.0 | |
| upper arm | 12.3 | 11.6 |
| forearm | 18.4 | 12.8 |
| hand | 21.4 | |
| thigh | 9.3 | 4.0 |
| calf | 8.8 | 2.9 |
| foot | 3.5 | |

So the difference is of both kinds: the arms are held 12 degrees differently (a copy of what each bone turns from
its reference pose would leave the hands 12 to 15 cm from where the clip has them), and thigh and calf agree in
direction to 3 or 4 degrees while their axes lie 9 apart (a copy of the rotations themselves would twist them).
`FSandboxClipPose` turns each body of the physics asset, in the space of the whole body, by what the clip's bone
has turned from its own reference pose, and first turns the segments of arms and legs to point as the clip's
skeleton holds them. Trunk, neck, collarbones, hands and feet keep their own reference: the body's own posture,
and feet that are flat when the clip's are. What comes out is one rotation for each of the 17 joints' bones, as a
pose written in joint angles gives; the bones between two bodies stay as they are, and the body of the head takes
the orientation of the head's own bone, so that the turn of the whole neck is in the one joint there is.

**What the clips ask of the joints**, the furthest beyond its range over the four get-ups (angles as the solver
limits them: twist, swing1, swing2, against the half-ranges):

| Joint | Beyond its range, degrees | Where |
| --- | --- | --- |
| hand | 73 | `_R` at 0.90 s: -60, 2, -98 against 70, 60, 25 |
| foot | 48 | `_F` at 1.83 s: -10, -73, 55 against 15, 25, 10 |
| upper arm | 43 | `_L` at 0.00 s: twist 123 against 80 |
| thigh | 29 | `_R` at 0.97 s: twist 74 against 45, swing2 -52 against 30 |
| forearm | 14 | twist 14 against a locked axis |
| `spine_02` | 14 | `_L` at 1.70 s: swing1 -31 against 18 |
| collarbone | 11 | swing2 33 against 22 |
| calf, `spine_04`, neck | 3, 2, 0 | |

Asked for as recorded, the muscles pushed the joints into their limits: mean pose error 7 to 12 degrees over a
film, a foot 3.7 degrees past its range while a pose was held. Every target is now held to its joint's range
(`FSandboxPoseRig::ClampToRange`, the box the solver limits by): mean error 2.9, 0.6 degrees past a limit. Two of
the rows are the body's model and not the clip. The wrist: the forearm's turn (palm up, palm down) is in the
wrist's twist, and the two swings are limited in the forearm's frame, so with the palm turned down the hand's
bending back is the "sideways" swing and ends at 25 degrees; a body that pushes itself up on its hands will meet
this in step 2. The foot: there is no toe joint, a foot "on its toes" in the clip is a rigid foot asked to stand
on its tip.

**What the pictures show** (`Tools\clip-film.ps1`; the body hangs by its welded pelvis, the weld carried as the
clip has the pelvis, 60 cm above the floor; `Saved\Films\clip_B_carried` and `_F`, `_L`, `_R`). All four read as
a person getting up, on this body: from the back a roll onto the side with the knees drawn up, one arm down, a
crouch, up; from the front a push-up onto hands and a knee; from a side onto hands and knees. The body stands by
2.2 to 2.5 s of the 5 s; the rest of each clip is standing still. The clip has the pelvis 11 cm above the floor
and 34 cm behind its root at the start (from the back) and 89 cm up over the root at the end. Played at its own
speed on the carried body, the muscles follow: mean pose error 3.8 degrees, no joint faster than 16 rad/s, none
more than 0.6 degrees past its range. With the weld at the clip's own height the body is pressed up to 10 cm
into the floor and its joints stretch by 5.8 cm: limbs in the way of a pelvis that gives way to nothing. That
is the rig, and says only that the get-up cannot be a pelvis dragged along the clip's path.

**The sample's other poses next to ours** (`Saved\Films\poses_fall_ours`, `poses_reach`, `poses_flail`,
`poses_onground`). Its nine reaching poses and three flailing loops are for a body thrown through the air: the
arms reach where the impact comes from, the legs trail, each pose is asymmetric as a recording is. The three
written here (`poses.fall_*`) are for a body going over from standing: backward it sits down and tucks its chin,
forward the hands come up in front of the face, sideways one arm goes out. Theirs are not better at that, and
they would need a blend aimed by the direction of the fall to be used at all; the fall keeps its own poses. The
sample's four poses for lying (on the back with the knees up, on either side curled, face down) are the first
frames of its get-ups; ours lies as it fell, with its muscles let go.

**A mistake found by a picture.** The first sheet of a carried body showed it standing at the end with its head
and trunk hanging forward. The controller had taken the body for fallen as the weld laid the pelvis down (tilted
by 35.6 degrees), and from then on the film showed the Lying state's muscles, a tenth as strong. Nothing failed,
since a film has no expectations. A body whose pose is asked for from outside no longer judges its balance.

New: `FSandboxClipPose`, a layer in `USandboxAnimInstance`, `USandboxBodyController::SetClipPose`, the scenario
event `clip_pose` and `follow_clip` on a pin, `Tools\clip-film.ps1`, scenario `clip_pose_hold` (the welded body
holds the clip's standing pose and then its crouch at 1.4 s: a calf's target 120 degrees from the reference pose,
mean pose error 2.4 degrees, nothing past a limit, no energy gained), three unit tests (31 in all). Against
`m3_part6_start` all 50 scenarios have the same hashes and one is new. Suite 51 of 51 with `-Long`,
push matrix 160 of 160, knockdowns 8 of 8 (as in part 5: 1.94 s, 25.9 J, 1.22 cm), session self-test 14 of 14.

### M3 part 6, step 2 (the GetUp state), 2026-11-24

A body that has lain still for 1.5 s gets up by one of the four clips, under full simulation. Seven scenarios:
a knockdown of 200 N*s on the chest from each of four directions and the get-up after it (`getup_back`, `_front`,
`_left`, `_right`), and three with something done to the body while it is on its hands and knees (`getup_push_weak`,
`getup_hit_strong`, `getup_held`).

| Scenario | Attempts | Stood after, s | Clip waited, s | Assist force share, mean and most | Joint past its range, degrees |
| --- | --- | --- | --- | --- | --- |
| `getup_back` (lies on its back, half on its right side; clip `_R`) | 1 | 5.2 | 2.0 | 0.47, 0.68 | 1.4 |
| `getup_front` | 1 | 4.0 | 0.8 | 0.44, 0.63 | 1.0 |
| `getup_left` | 1 | 4.7 | 1.5 | 0.47, 0.65 | 1.8 |
| `getup_right` | 1 | 4.9 | 1.7 | 0.44, 0.69 | 1.4 |
| `getup_push_weak` (30 N*s on the chest at 8.5 s) | 1 | 5.2 | 2.0 | 0.48, 0.68 | 1.5 |
| `getup_hit_strong` (200 N*s on the chest at 8.5 s) | 3 | 4.5 (the third) | 1.3 | 0.49, 0.77 | 1.6 |
| `getup_held` (two hands on the pelvis from 7.9 to 12 s) | 1 | 8.6 | 5.4 | 0.53, 0.68 | 1.3 |

Joints stretch by at most 0.3 cm and turn no faster than 38 rad/s; the body is at most 0.8 cm in the floor. The
recording stands at 2.5 s: the body takes twice as long, most of it on hands and knees while the pelvis rises
more slowly than the clip's.

**Three things that did not work, each found in the log line the controller writes four times a second** (the
clip's time, the pelvis and the feet against the clip, what the floor carries of each foot):

1. *The clip's time waiting for the pelvis alone.* The body rolled over, came onto hands and knees and stood up
   as a plank leaning 18 to 26 degrees, held by the assist with 0.9 of its weight, until the attempt was given up.
   Up to 1.3 s of the clip the feet were within 10 cm of the clip's; from 1.5 to 1.7 s the clip steps the right
   foot 45 cm forward, and ours stood where it was under 370 to 840 N.
2. *Waiting for the feet as well.* The clip stood at 1.50 s for good: the foot was 16 cm from where the clip had
   it (the limit was 15) and carried 525 N of a body that by then stood crouched on both feet.
3. *The clip laid over the feet the body stands on* (its root follows the feet that carry weight, in proportion to
   what they carry; only a foot in the air is waited for): the body stands, at the first attempt, from all four
   sides.

**The assist** was then swept down (`Tools\sweep.ps1`, six scenarios a setting):

| Pelvis cap, N | Feet cap, N | Gets up from four sides | Assist force share, mean | 200 N*s blow |
| --- | --- | --- | --- | --- |
| 500 | 200 (and lost at 45 cm) | yes | 0.61 to 0.76 | not felled: 40 cm off, back in 0.5 s with 1.2 of body weight |
| 500, 350, 250 | 200 | yes | 0.58 to 0.76 | felled (lost at 30 cm), up at the second attempt |
| 150 | 200 | no (back and right fail) | | |
| 250, 350 | 1 | no (only from the left) | | |
| 250 | 50 | yes | 0.44 to 0.48 | felled, up at the third attempt |
| 350 | 50 | yes | 0.46 to 0.51 | felled, not up within 24 s |
| any | any, pelvis pull 1000 N/m instead of 3000 | no (back and right fail) | | |

Taken: 250 N and 50 N, lost at 30 cm. The share is what the body's hands do not carry: arms of 200 N*m/rad with a
cap of 80 N*m and a wrist that cannot bend back with the palm down (step 1).

**What an interaction does** (FR-18). A hit of the flinch's threshold or more is felt as a standing body feels it,
without leaving the state: the struck part gives way and its muscles are weak for 0.35 s. The 30 N*s push delays
standing by 0.05 s. The 200 N*s blow puts the pelvis 31 cm from the clip's 0.09 s later and the get-up is given up;
the body lies on its back, tries by the clip for the back and is given up after the clip has stood at 0.76 s for
3 s (on its back with its knees up, it does not roll), rolls half over as it lets go, and gets up by the clip for
the right side. The hands land with 5 to 8 N*s each (felt as hits), press with 310 N on average for 4.5 s; the
clip's time creeps from 0.96 to 1.09 s, the pelvis stays 41 to 46 cm up, no joint is more than 1.3 degrees past
its range; 2.7 s after they leave the body stands.

Against `m3_part6_step1` the 51 scenarios before this step have the same hashes (a scenario's body gets up only
with `"get_up": true`) and seven are new: suite 58 of 58 with `-Long`. Push matrix 160 of 160, knockdowns 8 of 8,
unit tests 31, session self-test 14 of 14.

Seen and left for step 3: from a pose it did not fall into by itself (after the blow) the body needed three
attempts, and the clip for the back failed where the one for the side worked; the stand of 50 falls will say how
often. The films: `Saved\Films\getup_back`, `_front`, `_left`, `_right`, `_hit_strong`, `_held`.

### M3 part 6, step 3 (the stand of 50 falls), 2026-11-24

`Tools\getup-stand.ps1`: 50 falls drawn from a seed, each a blow or a shove (0, 0.1 or 0.2 s) of 200 to 320 N*s on
the chest or the head; 26 on the bare floor from any direction, and 8 each in the room of
`Specs\rooms\getup_room.json` with the body 45 to 90 cm from a wall, the table or the chair, knocked toward it
within 40 to 50 degrees. Table and chair are static boxes. A fall is a get-up when the body stands at the end of
40 s. The whole stand takes 90 s.

| | Falls that end standing | Floor | Wall | Table | Chair | At the first attempt |
| --- | --- | --- | --- | --- | --- | --- |
| First run | 35 of 47 (74%); 3 blows toward a wall were no falls | 23 of 26 | 4 of 5 | 4 of 8 | 4 of 8 | 18 |
| A foot is waited for 0.7 s at most; another clip after a failure; non-falls drawn again | 43 of 50 (86%) | 26 | 8 | 4 | 5 | 28 |
| The pelvis's lag judged by height (12 cm) and over the floor (12 cm) apart | 48 of 50 | 26 | 8 | 6 | 8 | 32 |
| ...height 12, over the floor 20 | 47 | 26 | 8 | 6 | 7 | 36 |
| ...height 12, over the floor 26 | 49 | 26 | 8 | 7 | 8 | 34 |
| ...height 16, over the floor 20 | 49 (98%) | 26 | 8 | 7 | 8 | 39 |
| ...and the body lost at 40 cm instead of 30 (taken) | 50 (100%) | 26 | 8 | 8 | 8 | 42 |

The four last rows differ by one or two falls, and which falls they are changes from row to row: on one seed that
is luck. On two more seeds:

| Seed | Height 12, floor 12, lost at 30 | Height 16, floor 20, lost at 30 | Height 16, floor 20, lost at 40 (taken) |
| --- | --- | --- | --- |
| 1 | 48 of 50, 32 at the first attempt | 49 of 50, 39 | 50 of 50, 42 |
| 2 | 45 of 50, 26 | 48 of 50, 36 | 49 of 50, 36 |
| 3 | 45 of 50, 37 | 48 of 50, 42 | 48 of 50, 42 |

With the values taken the body needs 1.28 attempts on average (seed 1) and stands 7 s after the blow at the median;
the assist's commanded force while it gets up is 0.51 of body weight on average over the 50 falls.

**The wider lags cost the weak push, and the suite said so.** With the clip allowed to run 16 and 20 cm ahead, the
pelvis is 18 to 23 cm from the clip's while the body rises, and the 30 N*s push of `getup_push_weak` put it 31 cm
off: lost at 30, the get-up was given up (the body got up again, but a weak push is to be ridden out). Lost at 35,
40 or 45 the push is ridden out and the 200 N*s blow still fells the body; 40 is taken. The suite's get-ups with
the final values:

| Scenario | Attempts | Stood after, s | Assist force share, mean and most |
| --- | --- | --- | --- |
| `getup_back`, `_front`, `_left`, `_right` | 1 each | 4.1, 3.3, 3.9, 3.9 | 0.45 to 0.48, 0.65 to 0.73 |
| `getup_push_weak` | 1 | 4.2 | 0.49, 0.73 |
| `getup_hit_strong` | 2 | 4.2 (the second) | 0.48, 0.77 |
| `getup_held` (two hands, 300 N each at most) | 1 | 5.4 | 0.51, 0.74 |
| `getup_pressed` (1500 N down on the pelvis for 4 s; new) | 2 | 3.4 (the second) | |

The two hands no longer hold the body down as they did in step 2: with the wider lags it rises under them, 1.3 s
later than unhindered. What it cannot lift is `getup_pressed`: the clip stands at 0.91 s for 3 s, the attempt is
given up with no joint more than 1.5 degrees past its range and the joints stretched by 0.06 cm, and the next
attempt, by another clip, stands.

**What the failures of the first run were** (from the logs of the runs):

- *The clip stood at its first key for 3 s, a foot 27 to 29 cm from where the clip has it, six attempts in a row*
  (three falls on the bare floor). The foot was off the floor, the leg across the other one: it does not come. The
  wait for a foot is now 0.7 s at most (`getup.foot_wait_s`).
- *The same clip, the same failure, five and six times.* The next attempt after one that was given up goes by
  another clip, if another starts at all like the body lies.
- *The clip stood at 1.2 s, the pelvis 13 to 15 cm from the clip's with a limit of 12, the body on its knees.* Part
  of that distance is over the floor (4 to 11 cm) and not height. Height is waited for within 16 cm, the place on
  the floor within 20.
- *Three of eight blows toward a wall felled nobody:* the body ended on its feet against the wall. Not a failure of
  the get-up, and not a fall: such a trial is drawn again (56 blows for 50 falls on seed 1).

**The falls that still do not end standing** are at the furniture (seed 1: none; seed 2: one at the table; seed 3:
one at the table and one at the chair). Other falls at the table do end standing, and the film of one
(`Saved\Films\stand_table`) shows how: the body slides under the table, and as the clip rises it backs out on
hands and knees and stands beside it. The body lies with its trunk under the table top or its legs through the
chair's, and the get-up, which is in place, has nowhere to go: the pelvis stays 20 to 30 cm below the clip's and
the attempt is given up, again and again. What would close them is the body moving out into open floor before it
rises, which this part did not build.

Against `m3_part6_step2`: the 51 scenarios without a get-up have the same hashes, the seven with one changed with the values of this step (all pass), and `getup_pressed` is new: suite 59 of 59 with `-Long`. Push matrix 160 of 160, knockdowns 8 of 8, unit tests 31, session self-test 14 of 14, the stand 50 of 50.

The stand is a step of `Tools\bootstrap.ps1` from now on.

### The stand's furniture in the middle of the room, 2026-11-24

On the owner's request the table and the chair stand in the middle of the stand's room, together, as a table and a
chair do; in step 3 each stood by itself near a corner (the table at (170, -120), the chair at (-170, 120)). Now
the table is at the origin of the room (600 by 500 cm) and the chair at (0, 50): drawn up to the table's +y long
side, its back away from the table, the front of its seat 6 cm under the table top. Nothing of the body or its
controller changed.

**Where the bodies stand.** `Tools\getup-stand.ps1` no longer has the furniture's coordinates written into it: it
reads where the pieces named `table` and `chair` stand from `Specs\rooms\getup_room.json` and places the bodies
around them. The body faces +y.

| Kind | Where the body stands | Knocked | The fall |
| --- | --- | --- | --- |
| table | 45 to 90 cm from the open long side (-y) | toward +y, within 40 degrees | forward onto the table |
| table | 65 to 110 cm from the -x end | toward +x, within 40 degrees | to its left, along the table |
| chair | 65 to 110 cm from the chair's +x side | toward -x, within 40 degrees | to its right, onto the chair and the table's corner |
| chair | 45 to 90 cm behind the chair | toward -y, within 40 degrees | backward over the chair's back onto the table |
| wall | 45 to 90 cm from a wall before or behind it, 65 to 110 cm from one at its side, in the four corner quarters of the room (140 to 220 cm from the middle along the long walls, 110 to 170 cm along the short ones) | toward the wall, within 50 degrees | as in step 3 |

**The result**, with the tuning of step 3 unchanged:

| Seed | Falls that end standing | Floor | Wall | Table | Chair | At the first attempt | Attempts a fall: floor, wall, table, chair | Blows drawn |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 49 of 50 | 26 | 8 | 7 | 8 | 39 | 1.04, 1.25, 3.00, 1.50 | 54 |
| 2 | 50 of 50 | 26 | 8 | 8 | 8 | 37 | 1.12, 1.75, 2.25, 1.50 | 57 |
| 3 | 50 of 50 | 26 | 8 | 8 | 8 | 43 | 1.08, 1.63, 1.88, 1.25 | 56 |

With the pieces apart it was 50, 49 and 48 (42, 36 and 42 at the first attempt). The difference of one or two falls
a seed is not a finding (a figure from one seed is luck, and so is one fall of 50); what three seeds do show is
that the table costs attempts: two to three a fall there, against one on the bare floor. A body that falls forward
onto the table lies across its top, or kneels at it with its trunk on the top, and the get-up in place has the
table in its way until an attempt that is given up has moved the body off it. The assist's commanded force while
getting up is 0.51, 0.51 and 0.50 of body weight on average, as before.

**What was checked besides the count.**

- *The 26 falls on the bare floor of each seed are bit for bit the falls of step 3* (78 of 78 hashes): the dice
  are rolled in the same order, and only the places in the room are other.
- *Every run repeats.* The three stands were run twice, the second time with films being rendered beside them:
  150 of 150 hashes the same.
- *No body ends on the furniture, and none in it.* The 47 falls at the table and the chair that end standing end
  with both feet on the floor beside the furniture (world positions from `trace.json`), and no body is ever more
  than 1.4 cm inside anything. One of them (seed 1, trial 36: 317 N*s on the head, forward) is thrown onto the
  table top, tries to get up there on hands and knees, slides off and stands on the floor at the third attempt.
- *The fall that does not end standing* (seed 1, trial 42: 245 N*s on the chest, forward onto the table's long
  side) folds over the table, slides down to its knees and ends on the floor with its pelvis under the table top:
  seven attempts, each given up with the pelvis 20 to 30 cm below the clip's. It is the failure of step 3, a body
  that does not move out into open floor before it rises.

**A body appeared with a finger in the furniture, before this change too.** A body is spawned in its reference
pose, the arms out, and brings them down within half a second: the fingertips are 57 cm to either side of its
axis and 26 cm ahead of it, 87 cm above the floor, below the top of the chair's back (93 cm). Step 3 put a body
as near as 45 cm to a thing at its side. In its runs `penetration.spawn_world_cm` reads 0.12 cm for a trial at the
chair (seed 1) and 0.79 cm for one at a wall (seed 2), where 0.034 cm is the soles on the floor; nothing looked
at it. A thing at the body's side is now 20 cm further off (65 to 110 cm), and the script refuses a place where
the box of the arms (58 cm to either side, 28 cm ahead, 12 cm behind), with 5 cm to spare, would touch a piece or
a wall; the first places written for the new layout were refused by it (the body beside the chair stood 34 cm
from the table's corner). Over the 72 room trials of the three seeds the figure is 0.034 cm.

**Falls at a wall and the furniture.** The falls at a wall are to be falls at a wall only. They are in the corner
quarters of the room, 130 cm and more from the furniture, and every run measures the nearest any body of such a
trial comes to a box of the furniture (`furniture_clearance_cm`: bone origins at the run's checkpoints, every
0.5 s; a hand reaches 25 cm further, so under 30 cm the script prints a warning). Nearest over the eight wall
falls of each seed: 66, 103 and 36 cm. The 36 cm is not the fall: that body (seed 3, trial 30) fell against the
wall 166 cm from the table and lay there; its first attempt was given up where it lay, the second and the third
were given up at once with the pelvis thrown 40 cm off the clip, each moving the body 55 to 65 cm toward the
middle of the room, and at the fourth it stood 30 cm from the table's end with its forearm 36 cm from the table
top. So a get-up that is given up moves the body by half a metre, and three of them in a row cross the room: in
a room of this size no place at a wall is out of reach of the furniture for good.

**A figure misread on the way.** `body.bodies[].final_location_cm` of a run is a body's centre of mass from where
the character was spawned, not a place in the world. Read as world coordinates it had eleven bodies "standing
inside the table" with their feet on the floor. The deepest penetration of those runs, under 1 cm, did not fit
that, and the trace, which is in world coordinates, had every one of them beside the furniture.

Changed: `Specs\rooms\getup_room.json` (two locations), `Tools\getup-stand.ps1` (places from the spec, the guard,
the clearance of wall falls, `-Film "<numbers>"`, and a film's camera kept inside the room's walls: placed 260 cm
out from the body as on the bare floor, it stood outside them for most places in the room). The films:
`Saved\Films\getup_stand_36`, `_38`, `_42` (table), `_43`, `_44` (chair).

Against `m3_part6`: suite 59 of 59 with `-Long`, all 59 with the same hashes (no scenario of the suite uses the
room; the physics asset, which the generator writes again on every run, is the same).

### M3 part 7, step 1, first half (the get-up's assist; a body pressed down), 2026-11-24

The owner's decisions before part 7: cut the assist of the get-up (a mean share of body weight of at most 0.3,
from 0.51), by correcting the wrist's model and giving the arms more strength; two hands hold a body down, one
slows it. What was measured turned the plan round: the assist's force went into something else, and the wrist is
a small part of it.

**The assist always pulls at its caps.** Swept down on the suite's get-ups, the mean share is 78% of what the caps
allow, whatever they are: the share measures the caps. So the question is what the body cannot do without the
pull. From its front it gets up with almost none (30 N on the pelvis, 20 N a foot: share 0.14); from its back and
its sides it does not.

**Where the pull went: dragging the body over the floor.** The log of the attempts that failed with a weak
assist, on the stand: the clip stood at its first key, the pelvis 21 to 35 cm from where the clip had it *over the
floor* and 4 cm in height, no weight on the feet. The clip's root is put where the pelvis is when the get-up
begins, and stays there unless feet carry weight. A body that turns into the clip's first pose, rolls onto its side
or comes onto hands and knees moves over the floor, and the clip then waited for the assist to drag the pelvis
back to a place that matters to nothing. `getup.pelvis_follow_s`: the clip's root follows the pelvis over the
floor, by the share of the weight that is not on the feet (the feet's share goes on following the feet, as since
part 6). With it at 0 a run is bit for bit what it was (59 of 59 hashes).

**The stand of 50 falls, three seeds each** (seeds 1, 2, 3; attempts are per fall):

| Arms while getting up | Assist caps: pelvis, feet (N an axis) | Clip follows the pelvis | Share | Falls that end standing | At the first attempt | Attempts |
| --- | --- | --- | --- | --- | --- | --- |
| 200 N*m/rad, cap 80 N*m (part 6) | 250, 50 | no | 0.51 | 49, 50, 50 | 39, 37, 43 | 1.46, 1.46, 1.32 |
| 1200, cap 120 | 250, 50 | no | 0.51 | 49, 49, 50 | 41, 39, 43 | 1.40, 1.44, 1.20 |
| 1200, cap 120 | 150, 50 | no | 0.43 | 47, 49, 49 | 34, 36, 35 | 1.70, 1.52, 1.48 |
| 1200, cap 120 | 90, 30 | no | 0.29 | 48, 49, 49 | 27, 25, 33 | 1.96, 1.90, 1.64 |
| 1200, cap 160 | 60, 20 | no | 0.20 | 43, 41, 39 | 13, 16, 21 | 3.12, 3.02, 2.46 |
| **1200, cap 120** | **90, 30** | **0.3 s (taken)** | **0.27** | **50, 48, 49** | **39, 38, 44** | **1.40, 1.48, 1.20** |
| 1200, cap 120 | 90, 30 | 0.6 s | 0.28 | 50, 49, 49 | 34, 36, 41 | 1.44, 1.42, 1.24 |
| 200, cap 80 | 90, 30 | 0.3 s | 0.27 | 48, 48, 50 | 34, 35, 39 | 1.68, 1.46, 1.30 |
| 1200, cap 120 | 60, 20 | 0.3 s | 0.19 | 50, 48, 49 | 21, 23, 26 | 1.90, 1.70, 1.58 |
| 1200, cap 120 | 30, 20 | 0.3 s | 0.14 | 46, 46, 48 | 19, 16, 15 | 2.24, 2.28, 2.04 |

With the clip following the body, an assist of 0.27 of body weight does what 0.51 did: as many falls end standing,
as many at the first attempt, as many attempts. Without the following the same caps cost half an attempt a fall.
Stronger arms are worth a tenth of an attempt a fall. Further down the body still passes the criterion (96% at
0.14), with more failed attempts, and a failed attempt is a body that flops back to the floor, which is not what a
person looks like: 0.27 is taken, and it meets the owner's aim. The four get-ups of the suite take 3.7 to 5.5 s
(3.3 to 4.1 with the stronger assist), and on the stand the body stands 8 s after the blow at the median (7 s).

**The wrist** (a variant spec with the wrist's second swing at 85 degrees instead of 25, the asset regenerated,
the spec then put back):

| Clip follows the pelvis | Caps | Falls that end standing, wrist as it is | ...wide | At the first attempt, as it is | ...wide | Attempts, as it is | ...wide |
| --- | --- | --- | --- | --- | --- | --- | --- |
| no | 60, 20 (arms cap 160) | 43, 41, 39 | 45, 48, 45 | 13, 16, 21 | 12, 19, 27 | 3.12, 3.02, 2.46 | 2.82, 2.46, 2.04 |
| no | 90, 30 | 48, 49, 49 | 48, 49, 49 | 27, 25, 33 | 29, 32, 39 | 1.96, 1.90, 1.64 | 1.84, 1.72, 1.38 |
| 0.3 s | 60, 20 | 50, 48, 49 | 49, 49, 49 | 21, 23, 26 | 26, 26, 30 | 1.90, 1.70, 1.58 | 1.84, 1.76, 1.56 |

A wrist that can bend back helps where the assist is weakest, and little once the clip follows the body. It was
not changed: the asset is as it was, and the 51 scenarios without a get-up are bit for bit the baseline. Whether
the wrist's model is corrected now or with M4's grab by the hand is the owner's to say.

**The elbow, as recorded, was not a defect of the joint.** The plan had "the elbow cannot straighten past its
reference pose". It can: the joint's range is 0 to 140 degrees from the straight arm, and with
`poses.stance.lowerarm_*.swing1_deg=-33` the standing body's arms hang straight (pose error 0.7 degrees). The
hanging arm is bent by 37 degrees because the stance does not ask for anything else. It is a change of a pose,
not of the asset, and is the second half of this step.

**What a blow does to a body getting up, with the clip following it.** The clip follows the pelvis with a lag of
0.3 s, a blow moves it faster. The second blow of `getup_hit_strong` with other impulses: 60 and 100 N*s are
ridden out, 140 N*s and more throw the pelvis 40 cm off the clip and the get-up is given up. The same threshold as
with the settings of part 6.

**A body pressed down** (owner's rule: one hand slows a get-up, two hold the body down). Three things were found.

- *With the weaker assist one hand stopped a get-up as surely as two.* The body came up into a crouch and stayed
  there, its pelvis 25 cm behind where the clip had it over the feet and 14 to 18 cm low, and the clip waited.
  Legs twice as stiff lifted it and spoilt the unhindered get-up (8.3 s instead of 4.7).
- *No strength of the body gives a clean line between one hand and two.* Its legs lift 600 N as they lift 300 N,
  or neither, depending on the phase of the clip the press finds it in. The line is now a rule: a body pressed
  with `getup.hold_force_n` (450 N) or more does not push on, its clip waits and the attempt is given up after
  `stall_s`; pressed with less, it gets up where the hand lets it (the clip follows the pelvis on its feet too,
  `pressed_follow`, and may run 22 cm ahead in height, `pressed_lag_cm`, since a body that pushes against a hand
  sags under it). What presses is the force felt in every frame of 0.3 s from other simulated bodies, the sense
  the body leans into a held push with. None of this acts on a body nothing presses: the stand's 50 falls of seed
  1 have the same hashes with and without it.
- *The scenario with two scripted hands had one hand on the body.* A scripted hand goes to a point of the world;
  the right one slid off the rising body and ended on the floor (its contact peak of 2360 N was the floor), and
  the left one alone was felt as 313 to 342 N: more than its spring's cap of 300 N, by its own weight and the
  body's push against it. A threshold of 300 N, tried first, therefore "held with two hands" a body that one hand
  pressed. The rule is now checked with scripted forces of 300 N and 600 N on the pelvis.

| Scenario | Presses | Felt at most, N | Attempts | Stood after, s |
| --- | --- | --- | --- | --- |
| `getup_back` (nothing presses) | | 0 | 1 | 4.7 |
| `getup_one_hand_slows` (new) | one scripted hand on the pelvis, to the end of the run | 342 | 1 | 5.0 |
| `getup_pressed_one_hand` (new) | 300 N down on the pelvis for 8 s | 300 | 1 | 4.9 |
| `getup_held` | two scripted hands for 4.5 s, one of which slides off | 340 | 1 | 5.3 |
| `getup_pressed_two_hands` (new) | 600 N down on the pelvis for 9 s, to the end of the run | 600 | 2, both given up as held down | does not stand |
| `getup_pressed` | 1500 N for 4 s | 1500 | 2, the first given up as held down | 3.7 (the second) |

New: `getup.pelvis_follow_s`, `hold_force_n`, `pressed_follow`, `pressed_lag_cm`; the expectation `max_get_ups`;
`control.get_up.pressed_max_n`; three scenarios. Changed values: the arms of `states.get_up` (1200 N*m/rad, cap
120 N*m), the assist's caps of `states.get_up` (90 N and 30 N).

Against `m3_part7_start`: 51 scenarios with the same hashes, the 8 with a get-up changed (all pass), 3 new: suite
62 of 62 with `-Long`. Push matrix 160 of 160 and knockdowns 8 of 8 with the figures of part 6 (their bodies do not
get up), unit tests 31, session self-test 14 of 14, the stand 50, 48 and 49 of 50.

Not done, and open: one hand slows a get-up by 0.2 to 0.6 s only, which may read as no effect at all (the owner's
to judge in a session); the films of the get-ups with the weaker assist (`Saved\Films\p7_getup_back`, `_front`)
read as before; of the 202 attempts of the three seeds 151 end standing, 22 are given up with the clip waiting,
most of them in the crouch at 1.0 to 1.5 s of the clip with the pelvis behind its feet, and 29 with the pelvis
40 cm off the clip (as many of each as with the assist of part 6: 34 and 25 of 211).

### M3 part 7, step 1, second half (the stance's elbow; a press on 50 falls), 2026-11-24

The owner decided, on the measurements above, that the wrist's model is corrected in M4 with the grab by the hand.
What is left of step 1 is the elbow, and it brought out that the figures for a pressed body had been one fall's.

**The stance's elbows: -20 degrees from the reference pose** (`poses.stance.lowerarm_l`, `_r`), which leaves 17
degrees of bend where there were 37. Chosen from pictures of the standing body from the front and the side at 0,
-15 and -25 (`Saved\Films\p7_elbow_compare.png`): as it was, the forearms are held forward of the thighs; at -25
the arm is a straight stick; a relaxed arm is in between. The hand's centre of mass is 9 cm further back and 4 cm
lower, beside the thigh. Against `m3_part7_step1a`: the 51 scenarios with a body under control all changed, as
they must (every one stands in the stance or falls out of it), and the 10 without one have the same hashes
(`drop_*`, `impulse_passive`, the portraits, the room and headset scenes).

| | Before | With the elbows at -20 |
| --- | --- | --- |
| Quiet-standing assist share, 10 s and 10 minutes (bound 0.0093) | 0.0067, 0.0066 | 0.0067, 0.0066 |
| Pelvis drift standing, cm | 0.91 | 0.91 |
| Push matrix | 160 of 160 | 160 of 160 |
| Knockdowns from 8 directions: slowest to rest, most energy gained, deepest in the floor | 8 of 8: 1.94 s, 25.9 J, 1.22 cm | 8 of 8: 2.50 s, 25.7 J, 1.33 cm |
| The stand: falls that end standing, at the first attempt, attempts a fall | 50, 48, 49; 39, 38, 44; 1.36 | 49, 50, 49; 41, 43, 45; 1.35 |
| Strikes on the forearm (`strike_punch_forearm`, `_heights`) | pass | pass |

**Three scenarios failed, none through the elbow itself: each was written for where one fall happened to put the
body.** With other arms the same blow fells the body another way (after the blow from the front it now lies on
its back and gets up by the clip for the back, where it had lain half on its right side and taken that clip).

- `getup_front` stood after 7.3 s instead of 3.7 and was in the middle of bringing its feet together when its
  16 s were over (`end_state` stagger). It has 20 s now.
- `getup_one_hand_slows`, written in the first half, pressed a scripted hand on the place where the body used to
  lie; the hand met it with 33 N*s and stayed in the air. Removed: a scripted hand cannot follow a body, and
  where a body lies changes with every change to it.
- `getup_pressed_one_hand` (300 N on the pelvis) took two attempts where it had taken one.

**A press on 50 falls instead of one.** That last failure asked how much of "one hand slows a get-up by 0.2 s"
was one fall's luck. `Tools\getup-stand.ps1 -PressN <force>` presses every fallen body down on the pelvis from
5 to 25 s and counts the bodies that stood while they were pressed.

| Press, N | Seed | Stood while pressed (5 to 25 s) | Stand at the end (40 s) | Attempts a fall |
| --- | --- | --- | --- | --- |
| none | 1, 2, 3 | | 49, 50, 49 | 1.40, 1.30, 1.36 |
| 300 (one hand) | 1, 2, 3 | 30, 35, 31 of 50 | 45, 46, 48 | 3.02, 2.32, 2.74 |
| 600 (two hands) | 1, 2, 3 | 0, 0, 0 of 50 | 44, 43, 44 | 4.68, 4.38, 4.44 |
| 300, with `pressed_lag_cm` 28 and `pressed_follow` 1 | 1, 2, 3 | 25, 31, 32 of 50 | 48, 49, 47 | 3.06, 2.52, 2.76 |

Two hands' worth keeps every body down, 150 of 150: that half of the owner's rule holds. One hand's worth does
not "only slow" a get-up: two bodies of three get up under it, with twice the attempts, and one of three stays
down until the press ends. The figure of the first half (0.2 to 0.6 s lost) was three runs of one fall. Wider
allowances for a pressed body change nothing. The single scenario now asks only that the body stands by the end
of its run; what a press of 300 N does is told by the stand.

Changed: `poses.stance.lowerarm_*` (-20), `Scenarios\getup_front.json` (20 s), `getup_pressed_one_hand.json`
(no limit on attempts), `Tools\getup-stand.ps1` (`-PressN`, `-PressFromS`, `-PressForS`); removed
`Scenarios\getup_one_hand_slows.json`.

Suite 61 of 61 with `-Long`, push matrix 160 of 160, knockdowns 8 of 8, unit tests 31, session self-test 14 of
14, the stand 49, 50 and 49 of 50.

### M3 part 7, step 2 (friction and restitution of the body's surface), 2026-11-24

The owner's decision before part 7: friction and restitution of the body against the floor are measured and chosen
before muscle strength and solver iterations are. Until now they were whatever the engine gave.

**What acted.** Every one of the NPC's 18 bodies had the engine's default physical material: friction 0.7,
restitution 0.3. The generator copies the template asset's material, and the MetaHuman template has none. Two
surfaces are combined into a contact's figures by the mode with the higher number of the two (0 average, 1 the
smaller, 2 the product, 3 the larger); everything asks for the average. The material's "override combine mode"
flags are read by nothing in the engine: a material's own mode always counts. The engine applies restitution only
to a contact closing faster than 5.6 cm/s (1000 cm/s² times the 1/180 s substep). A run now reports all of it:
`body.bodies[].material`, `friction`, `restitution`, and `body.surfaces`. Reading it changes nothing (61 of 61
hashes of `m3_part7_step2_start`).

**Tuning values.** `surface.friction` (every body but the feet), `surface.feet_friction`, `surface.restitution`,
`surface.friction_combine`, `surface.restitution_combine`. The controller makes two materials of its own, gives
them to its bodies as overrides and brings the solver's copy up to date when a value changes. With the values
that acted before (0.7, 0.7, 0.3, average) all 61 scenarios have the baseline's hashes.

**Friction of the pair, by experiment** (`Tools\surface-friction.ps1`: the body is knocked down, lies still and is
dragged by a steady force on its pelvis; from the trace, the friction is (F/m - a)/g while it is pushed and -a/g
after):

| `surface.friction` and `feet_friction` | Combine | Expected on the floor (0.7) | While pushed | After the push |
| --- | --- | --- | --- | --- |
| 0.7 (as before) | average | 0.70 | 0.69 | 0.69 |
| 0.3 | average | 0.50 | 0.51 | 0.50 |
| 0.3 | the smaller | 0.30 | 0.29 | 0.30 |
| 0 | average | 0.35 | | 0.36 |
| 0.5 | the product | 0.35 | | 0.36 |
| 1.1 | average | 0.90 | 0.89 | |
| 1.0 | the larger | 1.00 | 0.94 | 0.89 |
| 0.7, changed to 0.3 at 7.5 s of the run | average | 0.50 after the change | 0.51 | 0.49 |

A force of 500 N does not move the body and 560 N does (the threshold at 0.7 is 549 N). The settings mean what
the engine's source says, a change in the middle of a run reaches contacts that already exist, and at 0.9 and
more the figure reads low (limbs catch and roll). The first way tried, the push's impulse over the time the body
is in motion, read 0.55 to 0.58 at 0.7: the mass-weighted speed stays above its threshold while limbs settle, long
after the body has stopped. `tune_live_surface` holds the mechanism in the suite: a lying body whose friction is
taken in the middle of the run slides 7.1 m under a push of 300 N that would not move it otherwise
(`min_pelvis_drift_cm`, new).

**The sweep.** Figures are the pair's friction on the floor. Quiet standing, the push matrix and 8 knockdowns:

| Feet | Body | Restitution | Assist share standing | Push matrix | Knockdowns: pass, mean rest, ends this far away |
| --- | --- | --- | --- | --- | --- |
| 0.7 | 0.7 | 0.3 (before) | 0.0067 | 160 of 160 | 8 of 8, 1.61 s, 189 cm |
| 0.5 | 0.5 | 0.3 | 0.0067 | 159 (one fall at 80 N*s) | 8, 1.73 s, 200 cm |
| 0.9 | 0.9 | 0.3 | 0.0068 | 160 | 7 (one at rest after 3.8 s), 1.91 s, 181 cm |
| 0.9 | 0.5 | 0.3 | 0.0068 | 160 | 8, 1.67 s, 194 cm |
| 0.7 | 0.5 | 0.3 | 0.0067 | 160 | 7 (3.2 s), 1.74 s, 196 cm |
| 0.9 | 0.7 | 0.3 | 0.0068 | 160 | 7 (3.3 s), 1.84 s, 186 cm |
| 0.5 | 0.7 | 0.3 | 0.0067 | 160 | 8, 1.59 s, 188 cm |
| 1.0 | 0.4 | 0.3 | 0.0067 | 160 | 8, 1.83 s, 201 cm |
| 0.7 | 0.7 | 0.15 (0, average) | 0.0063 | 160 | 8, 1.65 s, 187 cm |
| 0.7 | 0.7 | 0 (0, the smaller) | 0.0061 | 160 | 8, 1.64 s, 187 cm |

Standing does not care about friction; without restitution the assist's share is a tenth lower. The push matrix
has the same counts in every row within two cells. The one knockdown over 3 s is another fall each time: a figure
from one fall. On 48 knockdowns (200, 260 and 320 N*s on the chest and the head) 42 to 44 pass at every setting
tried, the mean rest is 1.7 to 2.0 s and the mean energy gain 8 J; restitution changes none of it.

The stand, three seeds each (150 falls):

| Feet | Body | Restitution | End standing | At the first attempt | Attempts a fall |
| --- | --- | --- | --- | --- | --- |
| 0.7 | 0.7 | 0.3 (before) | 148 | 129 | 1.35 |
| 0.5 | 0.5 | 0.3 | 147 | 130 | 1.32 |
| 0.9 | 0.9 | 0.3 | 149 | 101 | 1.54 |
| 0.9 | 0.5 | 0.3 | 150 | 129 | 1.21 |
| 0.7 | 0.5 | 0.3 | 150 | 124 | 1.30 |
| 0.9 | 0.4 | 0.3 | 149 | 130 | 1.25 |
| 0.7 | 0.7 | 0 | 148 | 120 | 1.39 |
| 0.9 | 0.5 | 0 | 145 | 130 | 1.31 |
| 0.9 | 0.5 | 0.1 | 148 | 127 | 1.37 |
| **0.7** | **0.5** | **0.1 (taken)** | **149** | **127** | **1.33** |

One thing stands out of the noise (two or three falls, five first attempts): a body with 0.9 against the floor
gets up at the first attempt 101 times instead of 124 to 130. A body turning into a clip's first pose and coming
onto hands and knees has to slide over the floor. From 0.4 to 0.7 nothing differs.

**Grippy soles were not taken.** With the feet at 0.9 the stand was as good as any (150, 129, 1.21) and the
matrix passed, and two scenarios of the suite then failed in every combination with it: `push_thigh_200_stumbles`
fell (4 of 4 runs, 0 of 4 with the feet at 0.7), and `push_pelvis_80_side_steps` ended in Stagger (3 of 4, 0 of
4). The film of the second (`Saved\Films\sf_pelvis_side`): after two crossing steps the body stands with its legs
crossed, one foot against the other, and stays so for the 4.5 s left of the run. The matrix calls that "stepped".
In the matrices one run of 160 ends in Stagger at two of nine settings (one of them with the feet at 0.5) and
none at the others, so it is rare at any friction; it is a defect of leaving Stagger that this scenario shows
with these soles, and it is open (below). With the average, 1.1 is also what the two feet have against each other.

**Taken: `friction` 0.3, `feet_friction` 0.7, `restitution` 0.1 with `restitution_combine` 1.** On the floor that
is 0.5 for the body and 0.7 for the soles, and a restitution of 0.1 against anything that has more. The body's
0.5 is the middle of what is given for cloth and skin on a floor and well clear of the 0.9 that hinders getting
up; the soles stay where they were, since nothing measured asks for a change and more grip does harm; restitution
is near zero as a body's is, and no run could tell 0, 0.1 and 0.3 apart beyond noise. A search for published
figures gave the 0.5 that standards ask of a walking surface and nothing measured for cloth on a floor: the range
is the owner's. Dragged, the lying body now reads 0.51 to 0.54 (its feet carry part of it at 0.7).

| | Before | With the values taken |
| --- | --- | --- |
| Friction of the lying body on the floor, measured | 0.69 | 0.51 to 0.54 |
| Quiet-standing assist share, 10 s and 10 minutes (bound 0.0093) | 0.0067, 0.0066 | 0.0063, 0.0062 |
| Pelvis drift standing, cm | 0.91 | 0.91 |
| Push matrix; the 200 N*s row | 160 of 160; 15 stepped, 25 fell | 160 of 160; 12 stepped, 28 fell |
| Knockdowns from 8 directions: slowest to rest, most energy gained, deepest in the floor | 8 of 8: 2.50 s, 25.7 J, 1.33 cm | 8 of 8: 2.21 s, 25.5 J, 1.23 cm |
| ...mean rest, and how far from where it stood the body ends | 1.61 s, 189 cm | 1.55 s, 196 cm |
| 48 knockdowns up to 320 N*s, chest and head | 43 pass | 46 pass |
| The stand: end standing, at the first attempt, attempts a fall | 49, 50, 49; 41, 43, 45; 1.35 | 50, 50, 49; 38, 44, 45; 1.33 |
| ...on its feet after the blow, median and 90th percentile | 8.0 s, 14.4 s | 7.9 s, 18.4 s |
| The suite's four get-ups, s | 5.0, 7.3, 4.9, 4.7 | 5.4, 4.1, 5.7, 4.8 |

Against `m3_part7_step2_start`: the 51 scenarios with a body under control changed, as they must (each has
another surface), all pass; the 10 without one have the same hashes; `tune_live_surface` is new. Suite 62 of 62
with `-Long`, unit tests 31, session self-test 14 of 14. Baseline `m3_part7_step2`.

**How it looks** (`Saved\Films\sf_fall_new` and `_old`, `sf_getup_new` and `_old`). After the blow of
`hit_chest_200_falls` the body used to stay on its back with its knees up and its feet planted; now the legs
slide out within a second and it lies flat. The get-up of `getup_back` goes another way for it (over the front,
onto hands and knees) and takes 5.4 s instead of 5.0: one fall, and the stand says the two are level.

**For M4.** A lying body of 80 kg at 0.5 needs about 400 N to be dragged: one hand at its cap of 300 N does not
move it, two (600 N) do, with 2.5 m/s² to spare. At 0.7 it took 549 N, and two hands had 0.6 m/s².

**The stand runs shorter.** A trial ran for 40 s whatever happened, and most bodies stand 8 s after the blow. A
scenario can now end once every event is over and the body has been Standing for a time without a break
(`end_when_standing_s`; `Tools\getup-stand.ps1 -StopStandingS`, 3 s by default, 0 for the whole length). All 150
trials of three seeds agree with the full-length runs on who stood, after how many attempts and when; the mean
trial is 18.6 s. One stand takes 66 s instead of 90. `Tools\getup-seeds.ps1` runs three seeds side by side and
prints one line a seed: 106 to 114 s for the three.

**An engine process takes 1.75 GB.** Three stands side by side with 15 processes took 26 GB of the machine's 32
and a batch was stopped for want of memory. Nine processes (16 GB) leave room; `getup-seeds.ps1` has 3 a stand
by default.

**Open, found on the way.**

- *The crossed-leg stance that Stagger does not leave* (above): `push_pelvis_80_side_steps` with
  `-Tune "surface.feet_friction=1.1"` shows it.
- *A body that ends kneeling trembles.* One knockdown of 48 (260 N*s on the head from the front) ends sitting on
  its calves, propped on its hands, pelvis 40 cm up, and trembles at 4 to 6 cm/s around the rest threshold
  (0.05 m/s) for good: `rest_after_s` is null with restitution 0.3, 1.7 s without. Other fallen bodies lie at
  0.001 to 0.004 m/s.
- *Blows harder than the criterion's.* Of 48 knockdowns up to 320 N*s, 2 to 6 do not pass at any setting: at rest
  after 3 to 7 s, and one fall (320 N*s on the head, forward) with 50 J of energy gain. The criterion of part 5
  is 200 N*s on the chest, where 8 of 8 pass.

**Mistakes of mine.** A helper script had hash keys `s` and `S`, which PowerShell takes for one; the batch that
used it failed at once and was waited for. Five stand candidates were put in one batch where three would have
shown the trend. The first friction figure came from a measure that was not checked against a known case.

Changed: `Config/Tuning/body.json` (`surface`), `SandboxBodyTuning` and `SandboxBodyController` (`ApplySurface`),
`ScenarioRunnerSubsystem` and `ScenarioSpec` (materials in the result, `min_pelvis_drift_cm`,
`end_when_standing_s`), `Tools\getup-stand.ps1`; new `Tools\getup-seeds.ps1`, `Tools\surface-friction.ps1`,
`Scenarios\tune_live_surface.json`.

### M3 part 7, step 3 (the sweeps: solver iterations, muscles, assist), 2026-11-24

The owner accepted the surface values of step 2, and the sweeps were made on them. One value at a time, each
against quiet standing (`stand_npc`), the knockdowns from 8 directions and the push matrix (the 30 and 80 N*s
rows for muscles and assist, all four for the iterations); what looked like a candidate, against the whole suite
and the stand on three seeds. **No value was changed:** every one sits between two that are worse, or on a
plateau. The suite is bit for bit the baseline `m3_part7_step2`.

**Solver position iterations** (the owner's rule: the smallest count that passes every criterion and the matrix):

| Iterations | Assist share standing (bound 0.0093) | Pelvis drift, cm | Push matrix | Knockdowns | Physics step alone, ms a frame |
| --- | --- | --- | --- | --- | --- |
| 64 (as it is) | 0.0063 | 0.91 | 160 of 160 | 8 of 8 | 0.58 |
| 48 | 0.0068 | 0.91 | 158 | 7 (one at rest after 3.6 s) | |
| 40 | 0.0073 | 0.92 | 158 | 8 | |
| 32 | 0.0083 | 0.94 | 158 | 8 | 0.43 |
| 16 | 0.0136 | 1.07 | 160 | 8 | 0.36 |
| 8 | 0.0255 | 1.25 | 153 (5 pushes of 80 N*s held without a step) | 6 (2.4 cm into the floor) | |

Only 64 passes all of it. 16 and 8 break the bound on the assist's share; 32 to 48 keep it and lose the same two
cells of the matrix, mirror images of each other (30 N*s on the head from behind and to a side: two steps where
one is allowed). At 32 the stand is as it was (149 of 150, 126 at the first attempt) and two get-up scenarios of
the suite fail (`getup_held`: its scripted hand misses the body; `getup_pressed`: not yet standing at the end:
each one fall). 32 would save 0.15 ms of a frame of 11.1 ms.

**Those two cells are not a second step.** In every candidate that loses a cell of the 30 N*s row, the body makes
one recovery step and one step that brings the feet back, by its own log, and the run counts three: as the
stepping foot lands, the other one leaves the floor for 0.12 s and stands again 5.0 to 6.1 cm away. A step
measured from outside is a foot that left the floor for 0.1 s and stood again 5 cm or more away
(`StepMinLengthCm`), and this shuffle is just over it. (Four more candidates lose a cell on a blow to the head
from the side: three flights of a foot of 5 to 11 cm.) Judged again from the saved runs with a step counted from
10 cm, no cell of the 30 N*s row has a step too many at 16 to 64 iterations or in any muscle or assist candidate
but the one with no assist at all. At the values as they are, the same blow leaves the capture point under the
4 cm that call for a step, and the body does not step at all; with the pelvis's angular assist at half it is
4.4 cm out. The matrix's 160 of 160 has one row standing on two thresholds. Whether a shuffle of 5 cm is a step is the owner's
to say; the measure was not changed.

**Muscles** (stiffness at half and at one and a half or twice; damping at half and twice):

| Group | Value | What changed |
| --- | --- | --- |
| Legs, stiffness 800 | 400 | Pelvis drift 1.11 cm; 4 pushes of 80 N*s held without a step: legs too weak to step |
| | 1200 | A blow of 200 N*s on the chest no longer fells the body from two directions of eight: the table wants a fall |
| Spine, 600 | 300, 900 | Nothing beyond one knockdown over 3 s and one shuffle cell |
| Arms, 40 | 20, 80 | Nothing (fastest joint standing 1.62 and 1.40 rad/s against 1.43) |
| Neck, 40 | 20, 80 | One shuffle cell each |
| Damping: legs 0.06 | 0.03, 0.12 | Two knockdowns over 3 s at 0.03; 33.5 J of muscle work in a fall at 0.12 (25.5) |
| Damping: spine 0.06, arms 0.15, neck 0.1 | half, twice | One or two knockdowns over 3 s with the arms' at either; nothing else |

The legs are held from both sides; the rest is a plateau. "One knockdown over 3 s" is what one fall of eight does
by itself: three of the ten surface settings of step 2 had one.

**Assist.**

| What | Assist share standing | Pelvis drift, cm | Push matrix, rows 30 and 80 |
| --- | --- | --- | --- |
| As it is | 0.0063 | 0.91 | pass; 2 falls at 80 N*s |
| Pelvis horizontal 750 (1500) | 0.0051 | 1.12 | two shuffle cells |
| Pelvis horizontal 0 | 0.0033 | 1.54 | one shuffle cell |
| Feet 750 (1500) | 0.0048 | 0.91 | one shuffle cell |
| Feet 0 | 0.0030 | 0.91 | one shuffle cell |
| Pelvis angular 30 or 0 (60) | 0.0064 | 0.92 | one shuffle cell |
| Chest angular 0 (60) | 0.0063 | 0.92 | 6 falls at 80 N*s, one push of 30 held |
| Head angular 0 (10) | 0.0063 | 0.90 | two shuffle cells |
| Feet angular 0 (15) | 0.0064 | 0.91 | pass |
| Everything at half | 0.0037 | 1.12 | one shuffle cell; 4 falls at 80 N*s |
| No assist at all | 0 | 1.60 | 8 cells of the 30 N*s row fail (up to 7 steps after a blow on the head); 6 falls at 80 N*s |
| Stagger: pelvis torque cap 100 N*m (150) | 0.0063 | 0.91 | pass by the table, 6 falls at 80 N*s |
| ...cap 50 N*m | 0.0063 | 0.91 | 4 cells fail, 13 falls at 80 N*s |
| ...angular stiffness 500 (1000) | 0.0063 | 0.91 | as it is: the cap rules |

The body stands for 10 s with no assist at all. With everything at half the stand is level (148 of 150, 127 at
the first attempt) and `stand_foot_moved` falls. The cap of 150 N*m on the pelvis's upright hold in Stagger,
chosen in part 4 as the lowest that passes, is still that: at 100 three times as many pushes of 80 N*s end on the
floor.

Tooling: `Tools\suite.ps1 -Tune "<values>" -Label <name>` runs the whole suite against other values into
`Saved\Suites\<name>` and leaves the files baselines are made of alone.

### M3 part 7, before step 4 (what counts as a step; a body at rest off its support), 2026-11-24

Two things the owner decided on 24 November to have done before the clips: the push matrix counts a step from
10 cm, and the stance on crossed legs that Stagger did not leave is taken apart now.

**A step in the push matrix is a foot carried 10 cm or more** (`Tools\push-matrix.ps1 -StepMinCm`, 10 by default).
The flights of a foot are the run's own record (`control.steps`, from 5 cm); those of 10 cm or more are steps,
less the controller's regrouping steps of that length, and never fewer than the controller's own steps for
balance; the shorter ones are shuffles, counted in every cell and row and judged by nothing. No value of the body
changed.

| Check | Result |
| --- | --- |
| The values as they are | 160 of 160; against the matrix of `m3_part7_step2` 160 runs end the same way; shuffles 0, 3, 12 and 7 in the four rows |
| 32 solver iterations, the 30 N*s row | 40 of 40 (5 shuffles) |
| ...with `-StepMinCm 5` | 38 of 40: `push_neck_01_225_030` and `push_neck_01_315_030`, two steps where one is allowed, as in step 3 |
| `Tools\baseline.ps1 -CompareMatrix` | reads the new file as before (it compares outcomes and state sequences) |

**The crossed-leg stance.** `push_pelvis_80_side_steps` with `-Tune "surface.feet_friction=1.1"`: after two steps
across (the second lands at 3.57 s) the body stands on crossed legs, one foot 35 cm before the other and 1.5 cm
to its side, and is in Stagger at the end of the run with nothing in the controller's log after the second step.

*What held it.* Two questions were asked of two different supports, and the answers left a gap. A step is needed
when the capture point is more than `balance.recoverable_cm` (4 cm) outside the *whole soles* of the feet the
floor touches; Stagger ends when the centre of mass is over the part of the soles that *touches*. The body stood
on the edges of its feet. From 4.8 s to the end of the run, every frame:

| | cm |
| --- | --- |
| Centre of mass against the support that touches | 6.0 to 7.0 outside |
| Capture point against the same | 6.0 to 7.5 outside |
| Capture point against the whole soles (what the decision is made on) | 2.3 to 3.2 outside |
| Speed of the body, root mean square | 0.05 m/s, falling to 0.01 |

So no step (under 4 cm) and no Standing (not over its support), for good. A body cannot rest with its centre of
mass 6 cm off its support by itself: in Stagger the pelvis is held upright with up to 150 N*m, and 80 kg held
6 cm off is 47 N*m. The stagger's torque share was 0.11 of body weight times the height of the centre of mass on
average over the 5 s. The allowance of 4 cm is what ankles and hips bring back of a body in motion; at rest the
capture point is the centre of mass and nothing brings it back. This is the assist holding a body that needs a
step, which the owner's table calls a failure, in a place the table's check (`held`) does not see: the push
matrix read the run as "stepped". `series.csv` now has the decision's margin beside the other two
(`decision_margin_cm`, last column; all 62 hashes as before).

*How often.* The condition (in Stagger, both feet down, no faster than `balance.recover_speed_mps`, centre of
mass and capture point outside the support that touches) lasts at most 0.09 s in the 160 runs of the push matrix
and in no run longer; in the stuck run it lasts from 3.71 s to the end.

*The rule.* A staggering body in that condition for `balance.rest_step_s` (0.3 s: three times the longest
passing case, and about a person's reaction time) needs a step. Which foot and where is the stagger's own
planning: the feet take turns, so the foot that was crossed under steps out to its own side.

| | Before | With the rule |
| --- | --- | --- |
| `stagger_rest_off_support` (new: the same push, the soles made grippy by a console event at 1 s) | ends in Stagger, feet 46 cm from their stance | a third step at 4.01 s, 37 cm out to the side; Standing at 4.37 s; two regrouping steps; feet in their stance at 7.3 s |
| The same with `-Tune "balance.rest_step_s=0"` | | ends in Stagger: the scenario fails without the rule |
| Suite with `-Long` against `m3_part7_crossed_start` | 62 of 62 | 63 of 63; the 62 have the same hashes, one new |
| Push matrix | 160 of 160 | 160 of 160, every run ends the same way |
| 80 runs (80 and 200 N*s) with the soles at 1.1 | 2 end in Stagger | 1, and that one is a regrouping step that landed 0.14 s before the run's end; the rule asked for a step in 2 runs |
| Knockdowns | 8 of 8 | 8 of 8 (2.21 s, 25.5 J, 1.23 cm: the same) |
| The stand, three seeds | 50, 50, 49; 38, 44, 45 at the first attempt | 50, 50, 49; 38, 44, 45; the rule asked for no step in 150 falls |
| Unit tests, session self-test | 31; 14 of 14 | 31; 14 of 14 |

How it looks (`Saved\Films\stagger_rest_off_support\sheet.png`, from the front): crossed legs at 3.6 to 4.05 s,
the right foot out to the side at 4.15 to 4.25 s, a wide stance at 4.4 s, the feet together from 7.3 s.

Not changed, and seen on the way: the second step of that run is itself a step across. The left foot is put
before the right one because the capture point is heading 2 cm past the right foot's line, and a target past
that line makes a step a crossing one whatever the distance. It is what puts the feet in one line; the third
step now undoes it. Whether a target a few centimetres past the line should cross is a question of how the steps
look, for the clips.

Changed: `Tools\push-matrix.ps1`; `SandboxBodyController` (`UpdateState`: the rule), `SandboxBodyTuning`
(`balance.rest_step_s`), `Config/Tuning/body.json` and its `README.md`, `ScenarioRunnerSubsystem` (the series'
last column), `Scenarios\stagger_rest_off_support.json`, `Scenarios\README.md`.

### M3 part 7, step 4 (the figures of the gate, the clips), 2026-11-24

All on the build with the rule above; no value of the body changed in this step.

| What | Figure |
| --- | --- |
| Suite with `-Long` | 63 of 63 in 85 s |
| Unit tests; session self-test | 31; 14 of 14 |
| Push matrix (a step from 10 cm) | 160 of 160; 10 N*s: 40 stood; 30: 36 stood, 4 stepped; 80: 3 stood, 35 stepped, 2 fell; 200: 12 stepped, 28 fell; shuffles 0, 3, 12, 7; worst joint stretch 0.33 cm |
| Quiet-standing assist share (bound 0.0093) | 0.0063 over 10 s (0.0115 at most), 0.0062 over 10 minutes; pelvis drift 0.91 cm; the head travels 4.1 cm with the idle |
| Knockdowns, 200 N*s on the chest from 8 directions | 8 of 8; at rest after 1.26 to 2.21 s; 5.6 to 25.5 J of muscle work; 1.23 cm into the floor at most, 0.45 cm into itself |
| 48 knockdowns, 200, 260 and 320 N*s on the chest and the head | 46 pass; `neck_01` 315 degrees 260 N*s at rest after 5.62 s; `neck_01` 90 degrees 320 N*s gains 50.3 J |
| The stand, seeds 1 to 3 | 50, 50, 49 of 50; 38, 44, 45 at the first attempt; 1.36, 1.26, 1.38 attempts a fall; assist share 0.28, 0.28, 0.27 |
| The suite's get-ups from the back, the front, the left, the right | 5.4, 4.1, 5.7, 4.8 s, each at the first attempt, assist share 0.26 to 0.28 |
| The longest runs after 200 N*s (pelvis and thighs; 12 of 24 cells stay up) | 8 steps and 1.70 m (`thigh_r` 45 degrees), 7 and 1.86 m (`pelvis` 45), 7 and 1.87 m (`thigh_l` 135); 2.5 to 3.3 s in Stagger |

**The clips** are 25 sheets of pictures under `Saved\Films\m3_*` (listed in `Saved\Films\m3_gate.txt` and in the
M3 section of `docs/PRD.md`), made by one script that calls `Tools\film.ps1` for each; the camera of a fall, a
get-up and a run is put beside the way from where the body stood to where its pelvis ends. Runs of the push
matrix and trials of the stand are filmed from their generated scenario files (`-File`).

What the sheets show that the figures do not:

- *The fall that never stands* (`m3_getup_on_chair`, seed 3, trial 45): the body goes over the chair's back and
  hangs there bent double, its legs on one side and its trunk on the other. Each of 14 attempts is given up in
  the frame it starts in: the pelvis is 40 cm from where a clip that begins on the floor has it. It is not
  "under the table"; it is a body lying on a piece of furniture, for which there is no clip.
- *A get-up given up on the bare floor* (`m3_getup_given_up_floor`): on its back, the body lifts its
  legs and an arm and stays so for about 5 s before the attempt is given up (the clip waits for a pelvis 28 cm
  away); it drops back, and stands by another clip in 4.3 s.
- *The runs* read as a body carried off sideways with its arms out, stepping fast for a second and standing in a
  wide stance, then bringing its feet together.

Mistakes of mine in this part: the first cameras of the three runs stood 5.2 m away and the body was a fifth of
a tile high (filmed again from 3.6 m); the overlay sheets were first made from the `quarter` view, too far for
the text and the lines to be read (again from 3 m).

### M3 gate: the review of the clips, a bound on a get-up's waiting, one hand measured again, 2026-11-24

The owner handed the review of the clips and the open questions of the gate to the agent ("decide by the data and
the plan, and go on"). All 26 sheets were looked at. What follows is what the look found, what was changed for it,
and one figure of the gate that turned out to be stale.

**What the sheets show.** Pushes of 30 and 80 N*s, the held push, the hand that pushes the body back, the three
falls and the four get-ups read as a person: a head that gives way and comes back, one step and the feet brought
together, a lean into a hand and a lunge away from it, a fall onto the seat with the knees up or onto the forearms,
a roll onto hands and knees, a crouch, standing. The three runs after 200 N*s read as a body carried off sideways,
arms out, that steps fast for a second and a half, stands wide and brings its feet together by 5 to 6 s; nothing in
them calls for a limit of steps. Two things do not read as a person, both in get-ups that are given up:

- *At the table* (`m3_getup_given_up_table`) the body held one pose, propped on its hands with its back under the
  table's edge, from 10 s to 22.9 s: 13 s of an attempt of 15.6 s.
- *On the bare floor* (`m3_getup_given_up_floor`) it lies on its back with its legs and an arm raised for about
  4 s of an attempt of 8.5 s.

**Why an attempt could last 15.6 s with `getup.stall_s` at 3.** The stall is counted while the clip's time stands
still and is set back to 0 whenever the clip moves on. A body that hovers at the lag at which the clip waits lets
it move on by a frame now and then, and the count never reaches 3 s. Of 199 attempts on three seeds, 18 were given
up for a stall; they lasted 3.6 to 15.6 s, 6.6 s at the median, three of them over 10 s. Attempts that end
standing last 4.9 s at the median, 6.6 s at the 95th percentile, and 2 of 151 over 8 s.

**The rule.** An attempt is also given up when its clip has waited `getup.wait_s` in all, counting only the time in
which nothing presses on the body (a body that a hand slows is not hurried). Chosen on three seeds:

| `getup.wait_s` | End standing | At the first attempt | Attempts a fall | Longest attempt, standing / given up |
| --- | --- | --- | --- | --- |
| 0 (no bound) | 50, 50, 49 | 38, 44, 45 | 1.33 | 12.0 s / 15.6 s |
| 5 | 49, 50, 49 | 38, 44, 44 | 1.36 | 7.7 s / 7.3 s |
| 4 | 49, 50, 49 | 38, 44, 44 | 1.38 | 7.2 s / 6.8 s |
| 3 | 50, 50, 49 | 35, 38, 43 | 1.43 | 6.0 s / 5.8 s |

5 is taken: every attempt over 8 s is gone, the first attempts are as they were, and 3 costs eleven of them. The
one fall it costs (seed 1, trial 36, at the table) stood before at the second attempt after a long wait in which
the body backed out from under the table; with the bound it is at its fourth or fifth attempt when the 40 s end.
That is the limit of the furniture that is already written down, not a new one, and one fall of 150 is not a
finding by itself.

| Check | Result |
| --- | --- |
| Suite with `-Long` against `m3_gate_wait_start`, the bound off (`wait_s` 0) | 63 of 63, the same hashes |
| ...with `wait_s` 5 | 64 of 64; the 63 have the same hashes (no get-up of the suite waits 5 s), one new |
| `getup_wait_bounded` (new: the pelvis of the lying body is pinned to the world, `stall_s` set to 30 by a console event) | given up at 12.56 s, "the clip waited 5.0 s in all", a second attempt at 14.78 s; with `-Tune "getup.wait_s=0"` one attempt and the scenario fails |
| The pressed stand, 300 N, `wait_s` 0 and 5 | 13, 23 and 16 of 50 stand while pressed with both; 143 and 144 of 150 stand in the end |
| Unit tests | 31 |

A scripted force is felt as a press (it is what the stand's `-PressN` uses), so the first guard, a force of 1500 N
on the pelvis, did not count as free waiting and passed with and without the rule; a pin is not felt. The
expectation `min_get_up_attempts` is new for it.

How it looks now: at the table the body tries, drops, tries by another clip, and stands at 36.0 s as before
(36.5), with no pose held for more than 5 s; on the floor the first attempt ends at 13.8 s instead of 15.1 s and
the raised legs are held for about 4 s still. The wait itself is as it was: a clip asks for a pose the body cannot
reach from where it lies, and the body holds the nearest thing to it. That stays a known limit.

**One hand on a body getting up: the gate's figure was stale.** The gate's table said that under 300 N two bodies
of three get up. That was measured in step 1 and not again after step 2 changed the body's friction. Measured now
(the stand with `-PressN 300`, three seeds, bodies that stand while they are pressed):

| Surface | Stand while pressed, of 150 | Stand in the end |
| --- | --- | --- |
| As accepted in step 2 (`surface.friction` 0.3: 0.5 on the floor) | 52 (13, 23, 16) | 143 |
| `surface.friction` 0.7 alone | 103 (33, 35, 35) | 145 |
| Every value as before step 2 | 96 (30, 35, 31), the figure of step 1 | 139 |

So it is the friction, not the restitution: on a floor that gives 0.5 a pressed body's hands and knees slide, and
one hand keeps two bodies of three down for as long as it presses, where the owner's rule is that one hand slows
a get-up. Not changed here: the friction is a measured property of the surface, the lying body's drag (400 N: one
hand does not move it, two do) rests on it, and the hand itself, its force cap and its mass are tuned again in M4
on recorded motions. It goes to M4 with the grab, to be met by the rule for a pressed body
(`getup.hold_force_n`, `pressed_*`), not by the surface.

Mistakes of mine here: the first guard scenario pressed with a scripted force and could not fail; the helper that
ran the suite, the tests and a stand in a row was started in the background without a first run in the foreground
(it worked). And a mistake of the earlier parts that this one found: a figure of the gate was carried over from a
step before the body's surface changed. The figures of a gate are measured at the gate.

Figures on the final build: suite 64 of 64 with `-Long`, unit tests 31, the stand 49, 50 and 49 of 50 with 38, 44
and 44 at the first attempt and 1.36 attempts a fall; the push matrix, the knockdowns and the session's self-test
are in the table of the M3 section of `docs/PRD.md`.

Changed: `SandboxBodyController` (`UpdateGetUp`: the bound), `SandboxBodyTuning` (`getup.wait_s`),
`Config/Tuning/body.json` and its `README.md`, `ScenarioSpec` and `ScenarioRunnerSubsystem`
(`min_get_up_attempts`), `Scenarios\getup_wait_bounded.json`, `Scenarios\README.md`.

### M4 part 1, step 1 (the hands recorded and replayed), 2026-11-24

What a session's hands are asked for can be kept and replayed by a scenario. Nothing of the body or of an existing
scenario changed: the 63 scenarios of the baseline `m3_gate_wait_start` have its hashes, `getup_wait_bounded` has
the hashes of the commit before, and one scenario is new (`replay_demo_hands`).

**What is built.** The rig adds the target each hand was given to a recording on every `UpdateTargets`
(`ASandboxEffectorRig::SetRecording`), so only frames on which the world steps are kept, whatever source the
targets came from. The pawn starts and ends a recording (the K key, `StartRecording`, `StopRecording`) and hands a
finished one on; it writes no file and knows no character. The session (`USandboxPlaySubsystem`, `-Record=`) writes
it with a scenario that replays it and with what it saw of the outcome. A recording is text, one line a frame, 17
significant digits (a unit test reads one back bit for bit); 240 frames of two hands are 49 kB, a minute about
1.1 MB. A scenario names a recording in place of `effector` events (`"recording"`, `"recording_start_s"`), and the
runner plays it back frame for frame through `FSandboxRecordedTargetSource`.

**The check** is `Tools\record-replay.ps1`: a session without a person uses the hands alone (`-PlayDemo`: at frame
60 the recording begins, the right hand reaches into the chest from frame 90 to 115, the left from 170 to 190, the
recording ends at frame 300), the recording is replayed as a scenario of the same length, and the replay is held
against what the session saw.

| | Session | Replay |
| --- | --- | --- |
| States of the body | standing, flinch, stagger, standing, flinch, standing, stagger | the same |
| The frames they began in | 0, 99, 110, 161, 182, 214, 267 | 0, 99, 110, 161, 181, 213, 266 |
| Pelvis at the end, cm from the spawn point | 3.59, -6.77, 84.32 | 3.64, -6.51, 84.41: 0.27 cm apart |
| Right hand: first touched, impulse, speed of arrival | `spine_04`, 40.55 N*s, 3.26 m/s | `spine_04`, 40.55 N*s, 3.26 m/s |
| Left hand | `spine_04`, 14.84 N*s, 3.09 m/s | `spine_04`, 15.26 N*s (2.8% more), 3.13 m/s |
| Two replays | | the same hashes |
| The session a second time | the same recording, byte for byte | |

Up to and through the first hand's contact the two runs agree in every figure shown (the flinch in frame 99, the
stagger in 110, standing again in 161, the impulse to four digits), which is what says that the recording's first
frame is put on the right step of the body's life: one frame off would move all of them. After it they drift
apart by a frame and a quarter of a centimetre. A session and a scenario are not the same run: a session's process
runs no warm-up scenario (the first run of a process differs bit for bit from later ones), its hands exist from
its first frame and are made in another order relative to the body. The limits of the check (a frame, 1 cm, 5%)
were set after this one measurement, on this one scene, and are close to what was measured; a recording of a
longer or rougher motion will need wider ones, and what the check is for is a recording that is wired wrongly, not
the last digit.

**The plan said "the same hashes" and that was wrong.** A run's hashes are of every frame of that run, and a
session is another run. Written down in `docs/PRD.md` as it is: a replay ends as its session did within stated
limits, and a replay repeats bit for bit.

**What a recording cannot do.** It is made of points of the world, so against a body that moves otherwise the
hands reach for where the session's body was, as a scripted hand does. The drag handle, the impulse tool and the
reset are not in it (they join in part 5); a recording made across a reset is written with a warning. Pause and
slow motion are no hindrance.

| Check | Result |
| --- | --- |
| Suite with `-Long` against `m3_gate_wait_start` | 65 of 65; 63 with the same hashes, 2 new (`getup_wait_bounded`, as committed; `replay_demo_hands`) |
| Unit tests | 32 (one new: a recording comes back bit for bit, and is played back frame for frame) |
| Session self-test | 14 of 14 |
| `replay_demo_hands` (the demo's recording kept under `Recordings\`, 7 s) | hands deliver 55.8 N*s, 0.006 cm into the body at most, no tunnelling, the body staggers twice and ends standing |

Changed: `SandboxTargetRecording` (new), `SandboxEffectorRig`, `SandboxPawn`, `SandboxInteraction.Build.cs` (Json),
`ScenarioSpec`, `ScenarioRunnerSubsystem`, `SandboxPlaySubsystem`, `InteractionTests`; `Tools\play.ps1` (`-Record`,
`-Demo`), `Tools\record-replay.ps1` (new), `Tools\bootstrap.ps1` (a twelfth step); `Recordings\demo_hands.json`,
`Scenarios\replay_demo_hands.json`, `Scenarios\README.md`.

### M4 part 1, step 2 (the hands' source for a headset), 2026-11-24

The third source of the hands' targets is built and tested on made-up poses, and was then run in the headset
for a first look. What follows first is what was known from the engine's source and from tests before the
headset was on; "In the headset" below is what the first sessions showed.

**What was read in the engine (5.8.4), not remembered.** `IMotionController::GetControllerOrientationAndPosition`
has an overload that returns the linear and angular velocity with the pose; the OpenXR plugin fills it from
`xrLocateSpace` with a velocity structure chained on, for the time the frame will be shown at
(`FOpenXRHMD::GetPoseForTime`). The motion sources `LeftGrip` and `RightGrip` are the grip poses. Position and
linear velocity come in Unreal's axes, scaled by the world-to-metres value passed in; the angular velocity comes
as an axis times rad/s and is negated on the way, because the change of axes is a reflection, so `v = w x r`
holds with the engine's cross product as it is. A camera component locked to the headset has its relative
transform set to the head's pose every frame, so its parent is the origin of tracking space. With no mapping
context given to it before the session begins, the plugin makes an action of every controller key and sends
them as key events (`BuildLegacyActions`); its list of interaction profiles has no PlayStation controller, so
which profile's keys SteamVR fills for the PS VR2 controllers is not known here.

**What is built.**

| Piece | What it does | Knows of a headset |
| --- | --- | --- |
| `ISandboxControllerTracker` | "Where is the controller of this hand": pose, velocities, squeeze, in tracking space | no |
| `FSandboxTrackedTargetSource` | Pose through the play space's origin into the world; the body at an offset from the grip pose; its velocity `v + w x r`; the grip; what was tracked, counted | no |
| `FSandboxXRControllers` | Answers the interface from the engine: motion controller devices for the pose, the player's input for the squeeze; puts the tracking origin on the floor | yes, the only file |
| `ASandboxPawn` | A root of its own with the camera under it. In a headset: the origin of the play space, level, no flying and no mouse; the tracked source in place of the desktop one | asks `IsHeadsetOn` once |
| `USandboxPlaySubsystem` | Puts the play space 80 cm from the character; ends a `-vr` session that has no headset; does not hold the frame rate in a headset, only counts it; writes each hand's tracking into the session file | asks whether stereo is on |
| `Tools\play.ps1 -VR` | Leaves the OpenXR plugin on, passes `-vr`, prints what was tracked afterwards | |

New tuning values, all in one build with their names: `effectors.hand.offset_thumb_cm`, `offset_palm_cm`,
`offset_knuckles_cm` (0, 0, 0), `tools.headset.stand_distance_cm` (80), `tools.headset.hands_height_cm` (100).

**The unit test** (`DictatorSandbox.Interaction.Source.TrackedHandsFollowControllers`) gives the source made-up
controllers: a pose 30 cm ahead and 20 to the right of a play space that faces +y lands where it should in the
world with its velocity turned; the same three offsets put the two hands at mirrored places; a controller that
turns on the spot at 2 rad/s moves a body 3.6 cm off its axis at 7.2 cm/s; a controller that flies and spins about
an axis that is none of the three moves its target, over a step of 0.1 ms, by the velocity the target reports
(which ties the sense of the angular velocity to the sense of the rotations); a runtime without velocities gets
the difference of two poses, not across a pause, and the frames are counted; a lost controller leaves its hand
where it was, at rest, and comes back without a velocity from before; the grip is held to 0..1. With the cross
product's arguments swapped the test fails in three places, so it can fail.

| Check | Result |
| --- | --- |
| Suite with `-Long` against `m4_p1_step1` (saved before the change) | 65 of 65, all with the same hashes |
| Unit tests | 33 (one new) |
| Session self-test, without a renderer and rendered | 14 of 14 both; the picture shows the hands in front of the camera as before |
| `Tools\record-replay.ps1 -Twice` | 10 of 10 comparisons, every figure as in step 1 |
| `Tools\play.ps1 -VR` with no headset on | `PLAY ERROR` after 37 s: "-vr was asked for and no headset is showing the world" |
| `Tools\play.ps1 -VR -Smoke` | refused: a headset session has a person in it |

**One thing moved, by a sign of zero.** The pawn's view is now the camera component's pose under a root, not the
actor's own. The recording of the session without a person differs from the one kept in `Recordings\` in 480
characters: the hands' rotation has `0` where it had `-0`. Both replay to the same hashes
(`22131048`, `8c4513fe`), and the session's figures are the ones of step 1 to the last digit shown. The kept
recording is left as it is.

**What is not known until the headset is on** (step 4 looks at each first; the log line of each hand's first
pose and the `tracking` block of the session file answer the first three):

- whether SteamVR gives the PS VR2 controllers' grip poses and both velocities through this path;
- whether the squeeze arrives, and under which profile's key (the largest of four is taken);
- whether the headset runs at the simulation's 90 frames a second: at 120 the world would run a third faster
  than the clock, and the session's log has the rate every 900 frames;
- whether the spheres are where the hands are (the three offsets), and whether 80 cm is where the owner wants to
  stand;
- whether the HUD's text shows in the headset; nothing depends on it.

**In the headset, the same evening** (the owner put it on for a first look of a few minutes; four launches).

| Launch | What happened |
| --- | --- |
| 1 | The engine crashed 7 s after its start, in `XR_APILAYER_MBUCCHIA_toolkit.dll` (OpenXR Toolkit, an API layer on this machine since January that hooks every OpenXR application), called from the D3D12 RHI. `Tools\common.ps1` now leaves the layer out of a `-VR` run by the variable its own manifest names (`DISABLE_XR_APILAYER_MBUCCHIA_toolkit`), for that process only. M1's headset runs had gone through with the layer in |
| 2 | Ran; closed by accident after a minute |
| 3 | 79 s: both controllers tracked with both velocities from the runtime, 89.7 frames a second; hits on the neck and the chest, a fall and a get-up in 4.9 s. **No recording:** `-Record` asked the pawn to record when the session adopted it, before the pawn had made its hands, and the refusal was silent. The path had never been run (step 1 checked the one without a person). The session now starts the recording once the hands exist and says so, or logs an error |
| 4 | 72 s, recorded from the first frame: `Saved\Recordings\vr_check` (6506 frames, 2.2 MB) |

What the fourth session shows:

| Question of the list above | Answer |
| --- | --- |
| Grip poses and both velocities through this path | Yes: 4292 frames each hand, none without a velocity |
| The squeeze, and under which key | Yes, as `OculusTouch_Left_Grip_Axis` and `OculusTouch_Right_Grip_Axis`. It reads 0 or 1 and nothing between: the grip of these controllers is a button. No trigger key was seen (not known whether one was pressed) |
| The headset's rate | 89.9 frames a second against the simulation's 90 |
| A replay against its session | **The same to the last digit shown:** the 36 states of the body in the same frames, the pelvis 0.000 cm apart after being pushed a metre, the hands' impulses 175.92 and 494.48 N*s in both. The session without a person drifts by a frame and a quarter of a centimetre; this recording begins on the frame the hands are made, as a scenario's hands are |

Three things the recording shows that the plan did not expect:

- **The runtime's velocity trails the motion of the runtime's own poses by about 38 ms.** Held against the
  difference of the recorded positions, the recorded velocity fits best when the positions are taken 3.4 to 3.5
  frames earlier, and is then still smaller (slope 0.73 to 0.78; correlation 0.92 to 0.94 along x and y, 0.76
  to 0.84 along z; the axes and signs are right). Read as a pose extrapolated with its velocity, the lead is
  47 ms. So the pose is predicted for the time the frame is shown at and the velocity is not: the spring's damper
  is given a velocity that is late for the target it is given. "The velocity from the tracking rather than from
  the difference of two poses" was the plan's premise; whether the hand follows better with the one or the other
  is now a measurement, and step 3 makes it on this recording.
- **A controller out of the cameras' sight is not reported as lost.** From 62 s the right controller's pose
  wandered a metre and a half over five seconds, up to 199 cm above the floor, with a velocity of 0.01 m/s, and
  at 67.9 s jumped 116 cm in one frame to where the hand was. No frame counted as lost, the hand went after the
  jump at 13.1 m/s with the spring asking for 3437 N, and nobody had moved. Following a pose that is given but
  not marked as tracked was my decision of the afternoon, and it is wrong for this headset.
- **The two hands touched each other first** (26.7 N*s between them over the session): they are two bodies
  that collide, as designed, and a person brings them together without meaning to.

Also seen: the hands stood 22 and 25 cm from the controllers when these were first tracked, 24.6 s into the
session (they were asleep), and went there on their springs.

Mistakes of mine in these launches: after the first one I told the owner the session was running when it had
crashed 7 s in; I had looked for the line of its start and not at whether the process lived. `Tools\play.ps1`
reported that crash as "PLAY ENDED"; it now reports a crash as `PLAY ERROR` with the first lines of the stack,
and says so when `-Record` was asked for and nothing was written. A session can end by itself
(`-Seconds`, `-PlaySeconds=`), which is also how the recording from the first frame is now checked without a
person (3 s on the desktop: 270 frames, replayed to the same figures).

Mistakes of mine before the headset: the first unit test of the turning term asked for 0.01 cm/s where the arm's own turn
changes the answer by 0.09, and failed on its first run; a helper script was first written as a heredoc and broke
on an apostrophe, the pitfall that is written down.

Changed: `SandboxTrackedTargetSource` and `SandboxXRControllers` (new), `SandboxPawn`, `SandboxInteractionTuning`,
`SandboxEffectorRig.h` (a comment), `SandboxInteraction.Build.cs` (HeadMountedDisplay), `InteractionTests`,
`SandboxPlaySubsystem`; `Config/Tuning/effectors.json`, `tools.json` and `README.md`; `Tools\play.ps1` (`-VR`,
`-Seconds`, a crash and a missing recording reported), `Tools\common.ps1` (the API layer left out of `-VR` runs);
`Scenarios\README.md`.

### M4 part 1, after the first look in the headset, and step 3 (the hand against its target), 2026-11-24

**What the owner said of the first look** (asked one question at a time): the spheres sit on the fists, no offset
seen; the place is right, in front of the character at arm's length, the floor where the floor is; the spheres
follow the hands with no lag or tremble to the eye (the fastest swing was 3.9 m/s); both triggers were pressed
and neither reached the game, though the grips did; the HUD's text is drawn across both eyes, the left eye sees
the lines' beginnings and the right eye their ends, and cannot be read. The right controller's wandering pose at
the end of the session was the controller laid on a table and the headset taken off: no fault during play, and a
hand behind the back has not been tried yet.

**Changed for it, on the owner's decisions of the same evening.**

- *No text in the headset* (`ASandboxHUD` draws nothing there). The window on the monitor mirrors an eye, so it
  has no text either; what a session has to say is printed when it ends.
- *A controller the headset does not see leaves its hand where it was.* Only a pose the runtime marks as tracked
  is followed (`GetControllerTrackingStatus`); a pose given without that mark is counted (`estimated_frames`).
  Whether the runtime sets the mark as hoped is not known: the first session did not record it. For that the
  source also counts tracked frames in which the pose moved faster than 20 cm/s with a velocity under 3 cm/s
  (`moved_without_velocity_frames`), which is what the untracked pose looked like; on the recording that sign
  begins at 62.0 s, when the headset came off, and also marks 6 to 18 frames during play, so nothing acts on it.
- *A target does not jump.* Found again, tracked for the first time away from where the hand waits, or moved
  further in a frame than a hand moves (`tools.headset.max_hand_speed_mps`, 20), the controller is caught up
  with at `tools.headset.catch_up_speed_mps` (1.5) on top of its own speed. A velocity is not differenced
  across such a jump.
- *Every controller key that reaches the player's input is named at the end of a session* (the first version
  looked at grips and triggers only), for the trigger that did not arrive and for choosing a button.
- A run with hands writes `hands.csv`: each hand's gap to its target, its speed, the target's reported speed,
  the force the spring law asked for and the contact force, frame by frame. Read only: the suite's hashes are
  the baseline's.

**Step 3: how far the hand is behind its target** (`Tools\swing-lag.ps1`; the hand as it is, 1.5 kg on 3000 N/m
with a cap of 300 N). The right hand's target travels 60 cm with a minimum-jerk profile; the spring is told the
target's velocity in four ways. The gap is hand to target at the start of each step, the largest during the swing:

| Peak speed | exact velocity | difference of two poses | as the runtime gave it (38 ms late, 0.78) | none |
| --- | --- | --- | --- | --- |
| 2 m/s | 1.9 cm | 2.1 cm | 4.6 cm | 10.4 cm |
| 4 m/s | 4.5 cm | 5.3 cm | 11.6 cm | 19.3 cm |
| 6 m/s | 7.4 cm | 8.9 cm | 19.0 cm | 26.4 cm |
| 8 m/s | 10.5 cm | 13.1 cm | 26.6 cm | 32.1 cm |

After the target has stopped the hand is within 1 cm of it in 0.02 to 0.08 s with the exact velocity, 0.02 to
0.09 s with the difference, 0.02 to 0.14 s with the runtime's; it is furthest off by 0.2 to 3.0, 0.4 to 5.0 and
1.0 to 10.1 cm. The spring law asks for 67, 189, 365 and 581 N at the four speeds, more than its cap of 300 N
from 6 m/s, but only for a frame or two of a swing: the sweep further down shows that the cap is not what
holds a free hand back.

- **With the true velocity the gap is one frame of the target's travel** (2.2, 4.4, 6.7 and 8.9 cm at 90 frames a
  second) and little else. The hand is given the place for the start of the coming step and arrives there at
  its end. That frame is the floor of this design; a target led by its velocity over one step would take it
  out, and is a choice for part 2.
- **The difference of two poses is nearly as good as the truth, and the velocity as the runtime gives it is
  twice as bad.**

The same on the owner's recording (`vr_check`, 26 to 61 s, frames in which the hand touches nothing; the mean gap
by how fast the target moved):

| Velocity given to the spring | under 0.5 m/s | 0.5 to 1 | 1 to 2 | 2 to 3 | over 3 m/s | 95th percentile | largest |
| --- | --- | --- | --- | --- | --- | --- | --- |
| The runtime's, as recorded | 0.49 cm | 1.14 cm | 2.27 cm | 5.75 cm | 8.24 cm | 2.17 cm | 10.9 cm |
| The difference of the recorded positions | 0.23 cm | 0.71 cm | 1.40 cm | 3.18 cm | 4.72 cm | 1.30 cm | 5.9 cm |
| None | 1.13 cm | 3.53 cm | 5.86 cm | 10.95 cm | 14.06 cm | 5.71 cm | 17.5 cm |

About 5100 to 5600 frames each; 35 to 44 of them over 3 m/s, so the last column of speeds is thin. The three
replays part after the first contact, so they are not the same frames. Not measured: whether the difference of
predicted poses makes the hand tremble more (the owner saw no tremble with the runtime's velocity).

**What it means for the plan.** The owner's order for part 1 was "the hand's velocity from the tracking rather
than from the difference of two poses". On this headset the tracking's velocity trails its own poses, and the
difference follows them: a hand behind a fast target by 4.7 cm instead of 8.2. The source can take either or a
mix (`tools.headset.runtime_velocity_share`; 1, the plan's, until the owner decides).

**Starting point for part 2** (the force cap and the mass are tuned there): at a punch's 8 m/s the hand is 10 to
13 cm behind its target with a good velocity, nearly all of it the one frame. The recordings of step 4 are put
through the same tool.

**The cap, the mass and the stiffness hardly move a free hand** (the night of 24 November; the same swings, the
largest gap in cm at 2, 4, 6 and 8 m/s, with the exact velocity / with the difference of poses):

| Hand | 2 m/s | 4 m/s | 6 m/s | 8 m/s | Spring asked for, at 8 m/s |
| --- | --- | --- | --- | --- | --- |
| As it is: 1.5 kg, 3000 N/m, cap 300 N | 1.9 / 2.1 | 4.5 / 5.3 | 7.4 / 8.9 | 10.5 / 13.1 | 581 N |
| Cap 450, 600 or 900 N | 1.9 / 2.1 | 4.5 / 5.3 | 7.4 / 8.9 | 10.3 / 12.7 | 570 N |
| 1.0 kg (damping 0.0365 s) | 1.8 / 1.9 | 4.1 / 4.7 | 6.6 / 8.0 | 9.1 / 11.3 | 465 N |
| 1.0 kg, cap 450 N | 1.8 / 1.9 | 4.1 / 4.7 | 6.6 / 8.0 | 9.1 / 11.3 | 465 N |
| 6000 N/m (damping 0.032 s) | 1.8 / 1.9 | 3.8 / 4.4 | 6.1 / 7.4 | 9.1 / 11.4 | 882 N |
| 6000 N/m, cap 600 N | 1.8 / 1.9 | 3.8 / 4.4 | 6.1 / 7.4 | 8.5 / 10.4 | 823 N |

Three times the cap takes 0.2 cm off the gap at 8 m/s; a third less mass or twice the stiffness take about a
centimetre. In free air the hand's lag is the frame, not the hand. I had written "from 6 m/s the cap is what
holds the hand back" from the force the spring law asked for, without running the sweep: the law asks for more
than the cap for a frame or two of a swing, and that costs nothing to speak of. What the cap and the mass do
decide is contact: what a strike delivers and how hard a hand pushes. Part 2 tunes them on that.

| Check | Result |
| --- | --- |
| Suite with `-Long` against `m4_p1_step1` | 65 of 65, the same hashes (`hands.csv` is only read) |
| Unit tests | 33; the tracked source's test now also holds the lost pose, the catching up, a punch at 12 m/s that is not a jump, the waiting pose and the share of the runtime's velocity |
| Session self-test, `record-replay.ps1 -Twice`, `record-replay.ps1 -Recorded vr_check` | 14 of 14; 10 of 10; 9 of 9 |

Not run in the headset: the lost-pose rule, the catching up and the list of keys. The next launch there is a
minute of pressing every button, a hand behind the back, and taking the headset off with the session running.

Mistakes of mine here: the catch-up's first version let a differenced velocity of 104 m/s across a jump carry
the target over the jump in one step (the unit test caught it); the test's count of frames was off by one; a
string with a line break in it went into C++ through a heredoc, the written pitfall once more.

Changed: `SandboxTrackedTargetSource`, `SandboxXRControllers`, `SandboxPawn`, `SandboxPlayerController` (the
HUD), `SandboxInteractionTuning`, `InteractionTests`, `SandboxPlaySubsystem`, `ScenarioRunnerSubsystem`
(`hands.csv`); `Config/Tuning/tools.json` and `README.md`; `Tools\swing-lag.ps1` (new); `Scenarios\README.md`.

### M4 part 1, the night of 24 to 25 November (the owner away; decided by the agent on the data and the plan)

**Note of 26 November 2026:** Superseded in part on 25 November 2026 by "M4, the second look in the headset…": `effectors.hand.palm_collides` is 1, and every controller key, the triggers among them, arrives.

**The target's velocity comes from the poses** (`tools.headset.runtime_velocity_share` 0). The owner chose it for
the next launch, and the one thing that could speak against it was measured on the recording: whether a hand
told the difference of predicted poses trembles more. In the frames in which the controller moves under 5 cm/s
and the hand touches nothing (about 100 a hand: a person seldom holds still), the hand's speed is 3.1 and 3.3
cm/s on average with the difference of poses and 3.7 and 3.8 with the runtime's velocity (95th percentile 4.7
and 4.8 against 7.3 and 6.5); its speed changes by 2.6 and 1.8 m/s2 a frame at the 95th percentile against 2.1
and 1.3. No more tremble, then, on thin data; the owner's eye in the headset has the last word.

**A new take** (the owner's "a button that puts the character back and starts a new recording"). Which buttons
reach the game is not known (the triggers did not), so the take is asked for by what is known to arrive: both
grips held closed for a second with both hands raised above the eyes (`tools.headset.new_take_hold_s`,
`new_take_raise_cm`; closed grips alone would begin takes by themselves, since a person who punches holds both
closed); the controllers buzz when it begins. On the
desktop it is the N key. A take ends the recording that runs, puts the character back, makes the hands anew on
their targets and begins a recording of its own (`<name>`, `<name>_2`, ...), so that each recording starts with
a fresh body and replays as a scenario of its own.

What it took to make a take begin as a scenario begins:

- *The reset is carried out after the frame's physics* (`USandboxPlaySubsystem::RequestReset`, also for the R
  key). Put into the world in the middle of a frame, the character had half a frame of its own.
- *The hands are made anew* (`ASandboxEffectorRig::RemakeEffectors`), after the character, as a scenario's are.
- *What a take's session file says of the hands is the take's*, not the session's so far.

And how close it comes (`Tools\record-replay.ps1 -Takes`: three takes without a person, a touch of the chest a
second into each, left hand, right hand, left hand):

| Take | Session | Replay |
| --- | --- | --- |
| 1 (left hand) | flinch in frame 107, 22.38 N*s | frame 108, 21.79 N*s: 2.6% off, pelvis 0.55 cm apart |
| 2 (right hand) | frame 108, 21.90 N*s | frame 108, 21.84 N*s: 0.2% off, 0.71 cm |
| 3 (left hand) | frame 107, 22.37 N*s | frame 108, 21.79 N*s: 2.6% off, 0.39 cm |

Within the check's limits (a frame, 1 cm, 5%) and not to the digit, where the recording of a session's first
body, from its first frame, replays to the last digit (`vr_check`). It is not the hand: swapped, the odd takes
are off again and the even one is not, and the hand arrives at 3.25511 m/s in the session and in the replay. It
is the body: three frames into its life the pelvis of an odd take's body stands 0.03 cm from where an even
take's stands, and standing turns that into half a centimetre within a second, before anything touches it.
What makes every second body of a world differ was not found (the order in which the solver takes bodies that
are put into a world with a history is the guess; the alternation fits freed handles being used again). A shove
instead of a touch shows what it can do: the session's body took three steps where the replay's took one, on
the 4 cm that decide a second step. So a take's replay is a run of its own that begins as the take began: its
scenario's expectations are written from the replay, as for every recording.

**Toward part 2, without the headset** (the same night; nothing of the body's or the hands' physics changed:
the suite's 65 scenarios have the baseline's hashes after each of these).

- *The hand has a frame of its own,* whatever moves it: x along the forearm toward the knuckles and the fingers,
  z toward the thumb, y through the palm. A tracked hand is turned into it from the grip pose
  (`effectors.hand.grip_pitch_deg` -90, `grip_yaw_deg` 0, `grip_roll_deg` 0: by OpenXR's definition of the grip
  pose the fingers lie along its -z and the thumb along its x). Whether PS VR2's driver keeps to that definition
  is not known, which is why the three angles are tuning.
- *A hand is a fist or open, by its grip* (`effectors.hand.fist_grip` 0.5, with 0.1 to either side before it
  changes). Desktop and scripted hands are fists, the balls they have always been. An open hand shows a flat
  plate, 18 by 9 by 3 cm (`effectors.hand.palm_*`), in the hand's frame. **The plate collides with nothing:** it
  is there so that the owner can say, at the next launch, whether the palm faces the way the real palm does,
  before the plate becomes a shape. The shape itself was not built tonight: its orientation cannot be checked
  without the headset, and the runner's measures of a hand's penetration and tunnelling take the hand for a
  ball.
- *The controllers vibrate with what their hands press or strike with:* in proportion to the hand's own contact
  force, full at `tools.headset.haptic_full_n` (300 N, the spring's cap: a hand held against something is at full
  strength, a blow goes beyond it for a frame). From the hand's contacts, not from the body: the pawn knows
  nothing of what a hand touches. Not felt by anybody yet.
- *The body tells what happens to it* (`USandboxBodyController::OnBodyEvent`, `GetEvents`): every hit and every
  change of state, for sound, a face, a health system, a result file. A hit is told in every state, once a
  contact, when it has brought what makes a standing body flinch; a new hit is a contact after nothing was felt
  for the flinch's window, or a blow that lands while something presses already. A run's `control.events` lists
  them and `control.hit_events` counts the hits; the expectation `hit_events` holds the count (set in six
  scenarios: one slap is one hit, four punches four, a held force none, a blow on a body that gets up and
  another on it getting up two).

How the rule for a hit was found, since the first two were wrong: counted as the flinch counts, with the sum
set back at each telling, a shove of 200 N*s over nine frames was nine hits and a held hand eighteen; counted
once a stretch of rising force, a hand held at the chest was six hits in six seconds, because a hand presses
now more and now less. Once a contact, the hand is three: it lands, and the body steps away from it twice and
meets it again.

**A report on recordings** (`Tools\recording-report.ps1 -Name "takes*"`): every recording that matches is replayed
and read. On the owner's first recording it finds 13 hits in 72 s: the hand arriving at 0.3 to 4.1 m/s, 432 to 648 N
in the hardest frame of a strike (the spring's cap is 300 N: a moving hand gives up its momentum too), the neck
struck eight times, the chest four, the upper arm twice. It was this list that showed one strike told as two
hits: its force rose over two frames, the sum passed the threshold in the first and the second was a blow by
itself. A blow into a contact now counts only after the force of the hit told before it has stopped rising.
The ten minutes of step 4 are read by the same tool, take by take.

**The open hand as a body** (later the same night; behind `effectors.hand.palm_collides`, which is 0: nothing
of today changes until it is switched on).

Of the three ways to give one hand two shapes, the one taken has only one body in the world at a time and
leaves the fist's code untouched: the open hand is a second body, a box of the plate's size
(`USandboxPalmComponent`), made the first time a hand opens. When the grip opens it takes the ball's pose and
motion, its own spring (held at the point the ball's centre was) and its own mass (`palm_mass_kg`, 1.5 as the
ball's until part 2 tunes it); the ball stops colliding and being simulated, and follows as a marker. When the
grip closes the ball takes the hand back the same way. A hand that never opens never has the second body, so
every scenario of before has its hashes: 65 of 65. Whoever wants the hand asks the hand
(`GetHandTransform`, `GetHandVelocity`, `GetProbe`), not the component; the runner's measures of penetration
and tunnelling take the hand's own shape, with half the plate's thickness (1.5 cm) where the ball has its
radius. A scripted hand's keys say `"grip"` (1 a fist, the default; 0 open), and a turn puts the palm where it
is wanted.

Four scenarios with an open hand, in the suite:

| Scenario | What happens |
| --- | --- |
| `palm_slap_head` | The open left hand, palm leading, on the side of the head at 6.2 m/s: 7.2 N*s to the neck, one hit, a flinch, 0.00 cm in the body |
| `palm_slap_head_edge` | The same with the plate's thin edge leading: 10.2 N*s, 0.02 cm |
| `palm_push_chest_both` | Both open hands pushed into the chest and held: 30 and 33 N*s, 355 N at most, the body steps back and stands |
| `palm_open_close` | A hand that opens and closes in the air and then strikes the chest as a fist at 6.8 m/s: two changes of body, 15.5 N*s |

Tunnelling, as the ball was tried in M2: slaps on the head, the chest and the forearm, palm leading and edge
leading, with the target's speed raised and the force cap at 6000 N so that the hand can follow (30 runs; the
hand's speed is what it reached):

| Hand's speed | Deepest in the body | Tunnelled |
| --- | --- | --- |
| 6.1 m/s | 0.24 cm (chest, palm leading) | no |
| 10.0 m/s | 0.03 cm | no |
| 13.4 to 14.8 m/s | 0.03 cm | no |
| 17.8 to 19.6 m/s | 0.01 cm | no |

The measure follows the plate's middle, so it cannot see an edge that passes a limb: from 17.8 m/s the slap
aimed at the forearm with the palm leading touched the trunk first and never the forearm, where at 6 to 13.5
m/s it touched the forearm. That is twice a punch's speed; noted, not chased.

**A hand changes its body only where the other one has room.** The ball and the plate have other shapes: a
ball put where a plate lies flat on a chest would be 3 cm inside the chest and be thrown out of it, and the
fingers of a plate put where a fist rests against something would be 9 cm inside it. Before a change the hand
asks the world whether the other body, put where the hand is, would lie in anything
(`USandboxEffectorComponent::WouldOverlap`, a scene query); if it would, the hand keeps the body it has and
changes when there is room. A grip that closes while the open hand lies on something is counted
(`grips_on_body`, with `last_gripped`): that hand is not making a fist, and it is what part 3's grab starts
from. `palm_close_on_chest` holds it: the open hand pushed into the chest closes its grip there, stays a plate
(0.01 cm in the body at most), and is a fist once it has gone back.

Three more scenarios: `palm_close_on_chest` (above), `palm_slap_leg` (the open hand on the outside of the
thigh at 6.2 m/s; it meets the character's own hand first, as the ball's slap does, 23.6 N*s) and
`strike_punch_shoulder` (a fist from the front on the left collarbone at 8.7 m/s: 7.2 N*s, one hit, a flinch;
aimed 6 cm higher it only grazed, 4.1 N*s and no flinch).

**The owner's recording with open hands as bodies.** The recording of the first session was made before the
hand had a frame of its own, so a copy of it has its rotations turned by the tuning's quarter turn; replayed
with `palm_collides` on (`Tools\recording-report.ps1 -Name vr_check_plates -Tune ...`): 72 s of a person's
wrists, 14 changes between ball and plate (three squeezes a hand), 12 hits, no frame of tunnelling, 0.12 cm in
the body at most, the body on its feet at the end. The plates reach further than the balls did and land two
blows the balls missed. It is another run than the session, as it must be; what it shows is that a plate on
its spring is not thrown about by a real wrist. It assumes the quarter turn is right.

**A recording replayed with another hand is another fight after its first contact.** The owner's recording with
the force cap at 450 and 600 N and with a hand of 1.0 and 2.5 kg: 12 to 18 hits where the session had 13, the
hands' totals anywhere from 176 to 957 N*s, one run with a fall. The targets are points of the world; once the
body has gone another way the hands press on whatever is there now. So the hand's values are tuned on the first
contact of each take, which is what the takes of step 4 are for (one kind of strike a take, a fresh body each),
and not on a long recording's totals.

**The triggers that did not arrive: what SteamVR's own files say.** For the session SteamVR made a binding from
the engine's suggested bindings for the Touch profile and "auto-remapped" it to `playstation_vr2_sense`
(`Steam\config\openxr\system.generated.openxr.dictatorsandbox.unrealeditor-cmd.exe_oculus_touch_1_binding.json`).
In it the trigger is bound as the grip is: `/user/hand/left/input/trigger` pull to `oculustouch_left_trigger_axis`,
click and touch beside it; so are the sticks and the four face buttons. The controller's render model names L2
and R2 as its triggers and L1 and R1 as its grip buttons. So the runtime had the triggers bound, and the reason
they were not seen lies after it (the engine's key events, the window's focus at that moment) or in which
buttons were pressed. The next launch lists every controller key that arrives, which settles it.

On pictures of the bare scenario stage the plate is black on black (a flat face turned from the one light), so
the overlay's hands layer now draws an open hand's plate as its edges; the slap's picture shows it upright
beside the head, flat toward it.

Not known without the headset, and the reason the switch is off: whether the plate lies in the real palm
(the three angles), how an open hand feels against the body, and whether a plate on a spring that can also
turn (20 N*m) behaves when a person's wrist does what a script's does not.

**A second pair of eyes on the night's code.** With 41 files changed and nobody watching, the whole diff was
read by an agent that had not written it and was given the code, not my account of it. It found no crash, no
object used after its end in any order of destruction it traced, and the body's flinch logic untouched by the
events; and these, all checked against the code and the engine's source before they were fixed:

| Finding | What was done |
| --- | --- |
| The engine makes a vibration last one frame (`FOpenXRInput::SetHapticFeedbackValues` gives it the frame's own duration), and the pawn asked for one only when the strength changed: the buzz of a new take would have been an 11 ms tick, and a hand held at full force would have fallen silent after its first frame. The frequency of 1 was handed on as 1 Hz | A vibration is asked for in every frame it is to go on, and once more to stop it; the frequency is left to the runtime (0, OpenXR's "unspecified") |
| With the open hand a body: when the grip closes on something the hand stays a plate, but the plate was hidden because the grip was closed and the ball was hidden because the plate was the body. Nothing was drawn for as long as the player squeezed | What is seen follows the body that is the hand: the plate is shown while the hand is open or the plate is the body |
| The guard on a headset run looked for SteamVR's processes, not for a compositor that has finished coming up, which is the very thing that crashed the driver | It also refuses while the compositor is younger than 30 s, and while SteamVR's own log says anything but ready or standby |
| `play.ps1` took any call stack in the log for a crash; an ensure that the engine survives prints one too | A crash is a fatal error; a call stack is only quoted then |
| A new take waited for good for a reset that nobody carries out, in a session without the play subsystem | Without a listener the take begins as things are |
| Three constants of behaviour in code against the rule: the grip's slack, the gesture's grip threshold, the take's buzz | `effectors.hand.fist_grip_slack`, the gesture reads `fist_grip`, `tools.headset.take_buzz_s` and `take_buzz_strength` |
| A hand's `final_location_cm` was the marker ball's, a step stale, while the plate was the body | It is the hand's |

The reader also saw that a lost controller's target, found again, caught up in its place only and took the
controller's turn at once; the turn now goes the same share of its way in every frame and arrives with the
place. The grip is the controller's at once, as a person's squeeze should be.

**Before the next launch: a loss must not freeze a punch.** The rule of the evening leaves a hand where it was
as soon as the runtime stops marking its controller as tracked. Whether this headset drops that mark for a few
frames in a fast swing, when the hand leaves the cameras' sight for a moment, is not known: the first session
did not record it. If it does, the hand would stop in the middle of a punch and come after it at walking pace.
So a pose the runtime only guesses is followed for 0.15 s after the controller was last tracked
(`tools.headset.estimated_follow_s`; over such a time a gyroscope carries a pose well) and counts as a loss
after that, which still stops the wandering by a metre that took five seconds. The session's file counts both
(`estimated_followed_frames`, `lost_frames`), and the next launch says which of the two this headset needs.

Mistakes of mine on the way: a take's first version put the character back in the middle of a frame; the
session file of a take first carried the hands' figures of the whole session; I took the difference for the
hands' order of creation and for the first seconds of a process, and neither was it (a wait of 300 frames
changed no digit).

### M4 part 3, step 1 (the grab), the night of 24 to 25 November, behind a switch that is off

**Note of 26 November 2026:** Superseded in part: the grab is on by default since 25 November 2026 ("M4, the second look…"); a hold is felt (`reactions.hold_felt` 1, "M4 part 3, its two open questions"); since "R1: the led get-up" (26 November) one hand no longer slows a get-up, the first item of R2.

Built by the agent in the owner's absence, on the plan's own words ("a grip that closes on a body grabs it; one
that closes in the air is a fist"; "grab creates a constraint between the effector and the touched NPC body; the
force cap is what makes an 80 kg body heavy to lift and drag"). It is off (`effectors.hand.grab` 0) and every
scenario of before keeps its hashes (73 of 73 against the baseline `m4_p2_night`; the 82 with the grab's own are kept as `m4_p3_grab`, and a second run repeats all 82); eight scenarios `grab_*`
switch it on for themselves (nine with `grab_two_hands_one_arm`, added after the code was read by another). With it switched on for the whole suite, one scenario of the 80 changes, the one
whose grip closes on the chest (`palm_close_on_chest`). The owner's recording from the headset, replayed with
the grab on, has no grab in it and ends on the same hashes: fists that close in the air stay fists.

**What a grab is.** A joint (`FSandboxGrabJoint`) that keeps the hand's own point and one simulated body
together, both free to turn about it: a hand without fingers holds a place, not a direction. The joint has no
force cap of its own; the hand follows its target with its capped spring (300 N) and what it holds follows the
hand. A hand that holds something collides with no simulated body, and collides again once it has let go and
lies no deeper in one than `grab_clear_cm` (1). A grip takes hold when it closes (the grip passes `fist_grip` upward) with a simulated body within
`grab_reach_cm` (3) of the hand's shape, and takes the nearest. It lets go when the grip opens, when the hand has
been further than `grab_slip_cm` (45) from its target for `grab_slip_s` (0.25: the grip slips), when the joint
has carried more than `grab_max_force_n` (450) for `grab_overload_s` (0.1: the grip is torn open), and when what
it held is gone. The controllers vibrate with the force the hand pulls with, as they do with the force it
presses with.

**Three things the first version got wrong, each found by a run.**

- *Touch as the condition.* The first rule was "the hand touched the body within the last 0.1 s". The head
  could not be taken: the neck is soft, the head leans back from the hand that arrives, and at the moment the
  grip closed nothing was touching. A player in a headset has the same trouble from the other side (nothing
  tells a hand that it touches). The rule is reach now, asked of the world when the grip closes.
- *A skeletal mesh answers an overlap query as one thing.* The item index of the result, which the engine's
  comment says is the body's index, was -1; each body of the mesh is asked by itself
  (`FBodyInstance::OverlapTest`).
- *A hand that holds one segment of the trunk was caught against the next.* Jointed to `spine_02` and pulled
  toward the head, the hand's ball pressed against `spine_04`, and the joint carried 556 N of a 300 N hand
  until the overload rule tore the grip open, within a second. A hand that holds something now collides with
  no simulated body (it holds a point; what it holds does not move against it anyway) and collides again once
  it has let go and lies no deeper than a centimetre in one (in none, at first: a hand that had let go then
  sank into what it had held, toward a target a little way inside it). After that the joint carries the spring's force and no more: 294 N at most on
  average over a pull, with single frames up to 479 N (the hand's own 1.5 kg thrown about by a head that whips).

**The joint's force, and its unit.** `FConstraintInstance::GetConstraintForce` gives the joint's impulse over
the last substep, not a force (and not the "force times step squared" a comment in the solver suggests):
a hand that drags at a steady 231 N of its spring reads 124 kg*cm/s at 180 Hz, which is 223 N, the rest being
8 N of contact. `FSandboxGrabJoint::GetForceN` divides by the substep.

**The plan's row "grab and drag": lift an arm, pull a standing NPC, drag a fallen NPC 2 m with one and two
hands; joint stretch under 3 cm, hand force at its cap, no blow-up.**

| Scenario | What happens | Grab's joint | Body's joints |
| --- | --- | --- | --- |
| `grab_lift_arm` | The left forearm is taken from the side and lifted forward and up by 40 cm; the body stands, the arm follows and hangs again when it is let go | 0.003 cm, 141 N | 0.01 cm |
| `grab_palm_lift_arm` | The same with the open hand as a plate (`palm_collides`): the plate takes hold and stays the body while it holds | 0.003 cm, 86 N | 0.04 cm |
| `grab_pull_standing` | The forearm pulled to the side by 90 cm at 0.9 m/s: the body comes along in five steps, arms out, and stands 71 cm from where it stood | 0.005 cm, 314 N | 0.06 cm |
| `grab_drag_fallen_two` | A body on its back, two hands on the chest, pulled head first by 2 m in 5 s: it is dragged the 2 m, limbs trailing; each hand pulls with 196 and 171 N on average | 0.000 cm, 227 N | 0.06 cm |
| `grab_drag_fallen_one` | The same with one hand: the body moves 6 cm, the hand is left behind and its grip slips after 1.9 s | 0.000 cm, 303 N | 0.06 cm |
| `grab_getup_held` | A body that is to get up, held by the chest from before it begins for 6.9 s: one attempt is given up, it stands 2 s after the hand lets go | 310 N | 0.06 cm |
| `grab_overload_tears` | The two-handed drag with grips that may carry 150 N: both are torn open as the body begins to resist | 188 N | 0.06 cm |
| `grab_in_air` | A grip that closes 13 cm from the forearm takes hold of nothing | none | 0.00 cm |
| `grab_two_hands_one_arm` | Two hands on one forearm; the lower comes up into the upper one's place, the upper lets go there and leaves: it collides again only once it is clear of the other hand, and neither goes faster than 0.9 m/s | 205 N | 0.01 cm |

So M3's figure holds with a grab as it did with a scripted force: a lying body is dragged from about 400 N, one
hand does not move it and two do.

**`Tools\grab-pulls.ps1`: 93 pulls.** Seven bodies (forearm, hand, upper arm, head, chest, thigh, calf), five
directions, the hand's target leaving at 1, 2 and 4 m/s for 60 cm and staying there 1.5 s. Where each body's
surface is, the tool measures first (a hand of 20 N sent to the body's middle shows where it comes to rest),
so the pulls do not depend on where a change to the body puts an arm. 93 of 93 within the limits: the grab's
joint stretched 0.020 cm at most, the body's joints 0.16 cm, 17 J gained at most (the one fall), no body faster
than 5.6 m/s. What the body did: pulled by an arm, the head or the chest sideways, forward or back it comes
along in 1 to 6 steps and stands (one fall of 93: the upper arm pulled back at 4 m/s); an arm pulled up is
lifted; nothing pulled straight down or up by the trunk moves an 80 kg body, and the grip slips (43 of 93).

Two more rows of 31 pulls each, both within the limits: with the target leaving at 8 m/s (one fall, the same
upper arm pulled back; 442 N in the worst frame), and at 2 m/s with a grip that never slips
(`-Tune "effectors.hand.grab_slip_cm=0"`: the hand stays behind at its 300 N for the whole 1.5 s, nobody falls,
334 N in the worst frame). So the slip is a matter of where the player's hand is, not of the body's safety.

The reach, on 21 pulls (seven bodies, three directions, 1 m/s): with a reach of 0 cm nine take hold of nothing
(the upper arm, the head and the chest, which give way to the hand that comes), with 1 cm three (the chest),
with 3 cm and with 6 cm none, and no pull takes another body than the one it was sent to. 3 cm is the
smallest of these that takes every body.

**Open, written down for the owner and for part 3 proper.**

- *A standing leg does not come.* A thigh or a calf pulled by one hand in any direction stays where it is and
  the grip slips; the body does not step. During a pull of 276 N forward on the thigh the centre of mass is
  8 cm off the support that touches for a moment and the assist carries up to 0.26 of body weight. A person
  would hop. It is the body's side (what the assist may hold while a force is kept up), not the grab's.
- *The body does not feel a grab.* It feels contacts; a pull through a joint reaches it only as motion. So it
  does not lean into a pull as it leans into a push, and a get-up under a holding hand is not "pressed"
  (`getup.hold_force_n`). The first half of the remedy is built: the run and the session, which know both
  sides, tell the body every frame which of its parts the hands hold and with what force
  (`FScenarioStage::TellBodyOfHolds`, `USandboxBodyController::TellHolds`), and the body tells its listeners
  of a grab and of its end (`control.events`: `grab`, `release`; the expectation `grab_events`). The body does
  nothing else with it yet: no force, no target and no decision reads the holds, and every hash is as it was. A unit test
  holds the telling (`DictatorSandbox.Body.Events.HoldsAreToldAsGrabAndRelease`; 34 unit tests).
  What it should do with them (lean, count a hold as a press on a get-up) changes how the body behaves and is
  left for the owner to see first.
- *The stand itself* is as it was at M3's gate at the end of this night: 49, 50 and 49 of 50 on three seeds,
  38, 44 and 44 at the first attempt.

**One hand slows a get-up, two hold it: measured with hands, on the stand, on three seeds.** M3 left this rule
to be set again with the grab; it was last measured with a scripted force on the pelvis (52 bodies of 150
stood under 300 N). Two things were needed to ask it of hands. A scripted hand has to find a body wherever its
fall has put it: an `effector` event may say `"at_bone"`, and its keys are then measured from where that part
of the body is when the event begins (`Scenarios\README.md`; `grab_getup_held` uses it). And the stand of falls
got holding hands (`Tools\getup-stand.ps1 -HoldHands 1` or `2`, `Tools\getup-seeds.ps1 -HoldHands`): with the
grab on for the trial, one or two hands come down on the chest of the fallen body, close their grips and keep
their place for 15 s; so that they arrive on a body at rest, a held body begins its get-up after 4 s of lying,
not 1.5.

| Hands | Bodies taken hold of, of 150 | By the chest itself | A grip came off by itself | Pulled free and stood before the 15 s were up | Stand in the end |
| --- | --- | --- | --- | --- | --- |
| one | 146 (48, 48, 50) | 123 | 77 | 37 (12, 7, 18): a quarter | 141 |
| two | 146 (48, 48, 50) | 107 | 11 | 0 | 140 |

A body cannot stand with a grip on it that keeps its place 12 cm above where the chest lay: the grip slips
when the hand is left 45 cm behind. So "stood while it was held" is, for hands, "pulled free and stood before
the time was up", and every such body is also among those whose grip came off. Read that way the rule holds as
the grab is built, with no rule of the body added for it: from one hand at its 300 N half the bodies pull free
and a quarter are on their feet within the 15 s; from two hands 11 of 146 pull one grip off and none stands.
After the hands let go the bodies get up as they do unhindered (144 and 143 of 150 end standing, against
148). A hand takes the body nearest to it, and for a fifth to a quarter of the falls that was not the chest
itself but what lay on or beside it (the neck, an arm, the next segment of the trunk); the four bodies of 150
the hands did not take hold of had moved from where their chest was when the hands set out. One seed with the
hands sent to the pelvis instead: from one hand 23 of 50 pull free and 3 stand in time, from two 5 and none.

Read by another as well (the fourth reading of the night, of the hand sent to a part of the body and of the
stand's hands): nothing that could move a hash or crash; fixed were what the summary's words claimed, a
mistyped part's name that sent the hands to the middle of the room without a word, the two options
(`-PressN`, `-HoldHands`) given together, and which of two trajectories of one hand that begin together is
moved.

In the one fall there is a scenario for (`grab_getup_held`), one hand on the chest keeps the body on its hands
and knees for as long as it holds; the attempt is given up as the hand lets go, and the body stands 8 s later.
- *On the desktop the G key holds the hands open,* so that the grab can be tried without the headset
  (`Tools\play.ps1 -Grab`): G held, a mouse button held to reach, G let go while the
  hand presses on the body, and the hand holds; G again lets go. The grip has to close while the hand still
  presses: a body that is pushed gives way, and half a second later it stood 3 cm out of the hand's reach (the
  first run of the check below). The proper verbs are part 5.
- *In a headset the take's gesture and the grab share the grips.* They do not collide (a grip that closes in
  the air above the head takes hold of nothing), but a button is still the better way once it is known which
  ones arrive.
- *A hand that holds something passes every simulated body,* the other hand and, later, props among them. With
  held objects (part 4) this becomes a matter of pairs.
- *The path "what it held is gone"* (a reset or a new take while a hand holds the character) no scenario
  reaches, since a scenario has no reset. A session without a person does (`Tools\play.ps1 -Demo -Grab`, a step
  of the bootstrap): the right hand takes hold of the chest, the character is put back while it is held, the
  hand has lost what it held and holds nothing, comes back, takes hold of the new character, pulls and lets go.

**The code read by another, again.** The same reading as for the evening's code, of the grab alone, before any
of it was called done. No fault in the lifetimes (every order of destruction traced, the engine's handling of
a joint whose body is removed checked in its source) and none in the path taken with the grab off. Found and
fixed: two hands that overlapped were set back to colliding inside each other, because the test for "clear"
left the whole rig out (it leaves out only the hand's own two bodies now; `grab_two_hands_one_arm`); the force
a hand reports kept its last value after a snap and would have gone on vibrating the controller; the pulls'
tool gave 0.5 and 1 m/s the same scenario name, accepted a hold of 0 s and reported "0 of 0" as passed; a prop
of another object type could have been taken by a hand that still collided with it (only what a holding hand
passes is taken now); a hold that ended by a slip was counted as a grip that closed on a body. And one thing
about the engine: a joint's force is kept for the last substep of a frame only, so the "largest force" of a
grab is the largest at the end of a frame.

**And a third time, for what was written after that reading** (the hold events, the hand that collides again,
the G key, the session's grab demo, the tools). Nothing that touches a run with the grab off. Found and fixed:

- *The kept recording of the desktop's hands had every grip at 0.* It was made in step 1, before a hand had a
  grip; since the evening a desktop hand is a fist with a grip of 1. Replayed, the kept recording had open
  hands for the whole run, which changes nothing while the open hand is only shown, and would have made
  plates of them with `palm_collides` on. `Recordings\demo_hands.json` is made anew; `replay_demo_hands` keeps
  its hashes.
- *A recording carries the grip and not what the grip could do.* A session started with the grab on, replayed
  from its recording with the tuning files as they are, closed its grips on nothing, and nothing said why. A
  session's replay scenario now switches on, before its first step, what its own hands had switched on (the
  grab, the open hand as a body); checked by a session recorded with both on (9 of 9 comparisons hold).
- *The check `grab_events` promised more than it tested:* "each grab has ended or is still held" was true by
  the way the body keeps its list. It now holds that what the body was last told is held is what the hands hold
  at the run's end, and the body is told once more as a run ends (a grip that closed in the last frame was
  counted by the hand and never told).
- Smaller: a snap during a hold was counted as a grip closed on a body; the depth at which a hand collides
  again may be 1.5 cm at most (half the open hand's thickness); the pulls' tool refuses speeds that round to
  the same tenth and a distance of 0, and prints 0.5 m/s as 0.5; `Tools\play.ps1 -Grab` without `-Demo` did
  nothing and is now a person's session with the grab on.

### M4, what the hands do as they are tuned, and the leg that does not come (the night of 24 to 25 November)

**Note of 26 November 2026:** Superseded in part on 25 November 2026: a fist has a blow (20 to 154 N*s at 3 to 5.5 m/s, "The punch, how far a fist is carried…"); the 80 N cap fails `grab_pull_standing` and was not taken, while `reactions.hold_felt` makes a pulled leg step in 9 of 24 ("M4 part 3, its two open questions").

Two measurements with no value changed, made after the grab, on the question the plan leaves to part 2
("the fist's mass, the force cap and the stiffness, on the recordings") and on the oddity the grab's pulls found.

**A hand that goes on through the body fells it, as it is tuned.** Scripted hands from 70 cm in front of the
chest to 150 cm behind it (a shove) at 1, 2 and 4 m/s with one hand and with two, and punches at the head and
the chest that go 40 cm in and stay a second, at 6 and 10 m/s; each with the hands as tuned (1.5 kg, 300 N),
with a cap of 600 N, with 600 N and 3 kg, and with 1200 N and 3 kg (40 runs).

| What | As tuned | With the stronger and heavier hands |
| --- | --- | --- |
| A shove at 1 m/s, one hand or two | three steps back, then a fall; 262 to 273 N*s | the same; 258 to 272 N*s |
| A shove at 2 and 4 m/s | one step and a fall; 133 to 154 N*s | none or one step and a fall; 145 to 197 N*s |
| A punch into the chest that stays in | one step and a fall; 114 to 116 N*s | the same; 130 to 140 N*s |
| A punch into the head that stays in | two steps, stands; 76 to 77 N*s | two to four steps, stands; 65 to 94 N*s |

So what fells the body is not the hand's strength but how long it goes on pushing: 300 N for half a second is
150 N*s, and the push matrix has the body fall from 200 N*s and step from 80. A punch that comes back at once
(the suite's strikes, and every punch of the owner's first recording) delivers 12 to 28 N*s and makes it
flinch. Twice the cap and twice the mass change the peak force of the contact (up to 3200 N from 1750) and
not the outcome. For the tuning of part 2 this says: the cap and the mass decide how hard a quick punch is; a
knockdown is already there for a hand that follows through. Two scenarios hold it, one for each verb of the
plan that had none with hands: `effector_push_chest_falls` (the push: three steps back, then the fall) and
`effector_shove_knockdown_gets_up` (the knockdown: two hands, the fall, the get-up).

**A standing leg that is pulled does not come: mostly because a hand is not strong enough.** A foot that
carries half the body presses on the floor with about 400 N, and at a friction of 0.7 for the soles it takes
280 N to slide it; the hand has 300. The balance assist adds to that: its pull on the pelvis carried up to
0.26 of body weight during a pull of 276 N on the thigh. With that pull capped (`assist.pelvis.max_force_n`,
which is 0, no cap):

| Cap on the pelvis's assist | A thigh pulled forward | A thigh pulled sideways, a calf pulled sideways or forward | Push matrix |
| --- | --- | --- | --- |
| none (as tuned) | stands, the grip slips | stand, the grip slips | 160 of 160 |
| 200 N, 120 N | stands, the grip slips | stand, the grip slips | 160 of 160 (four more shuffles in the 80 N*s row at 120 N) |
| 80 N | comes along in three steps | stand, the grip slips | 160 of 160 (the same four) |

Not changed: the stand of get-ups and the knockdowns were not run against a cap, and whether a body should
come along by its thigh under one hand is the owner's to say. If it should, 80 N is where it begins, at no
cost to the push matrix. (Corrected on 25 November 2026, noted on 26 November: the 80 N cap fails `grab_pull_standing`, makes pulls on an arm worse and brings 2 of 24 legs along; the larger cause was that the body did not feel the hold; see "M4 part 3, its two open questions".)

### M4 part 5, a first step: the kicking foot (the night of 24 to 25 November)

**Note of 26 November 2026:** Values superseded on 25 November 2026: the foot is 10 kg on a cap of 3500 N and 24000 N/m ("M4, doubling over…"); the desktop kick is a level thrust that reaches 150 cm ("M4, the desktop's kick as a thrust…"; `tools.kick.reach_cm` 150).

The kick was the one verb of the plan with no way to be made at all. It is an effector of a kind of its own:
`foot_left` and `foot_right`, the same body on the same spring as a hand (`USandboxEffectorComponent`), with a
leg's mass and strength (`effectors.foot.*`) and none of what makes a hand a hand: it has no grip, no open
shape and no grab, and is always a ball. A scenario scripts it as it scripts a hand. No scenario of before is
touched (their hashes are the same).

**The values, and what decided them.** 5 kg (the lower leg and the foot), a ball of 6 cm, a spring of
10 000 N/m (the hand's natural frequency at this mass), and a force cap that was measured: with 900 N, three
hands' worth, a kick that goes 30 cm through the trunk and stays 0.15 s delivers 152 N*s and the body takes five
steps and stands, and 214 N*s into the thigh drive it back ten steps; with 1800 N the same kick through the
trunk delivers 173 N*s in less time and fells it, with 8 kg and 3000 N likewise. 1800 N is about what a leg
pushes with, and it is the smallest of the three that gives the plan's "or the NPC falls". Each is the
owner's to change.

| Scenario | The kick, at 8 m/s | What happens |
| --- | --- | --- |
| `strike_kick_thigh` | from the front 10 cm into the left thigh, and back | 61 N*s; the thigh gives 23 cm, three steps, stands |
| `strike_kick_calf` | from the front 10 cm into the left calf, and back | 51 N*s; the calf gives 30 cm, the body stands without a step |
| `strike_kick_torso` | from the front 10 cm into the belly, and back | 73 N*s; two steps, stands |
| `strike_kick_torso_through_falls` | the same, 30 cm through and held 0.15 s | 173 N*s; one step and a fall, at rest after 1.5 s |

All four: no tunnelling, 0.00 cm in the body, the fastest joint 5 to 21 rad/s (the limit is 30), joints
stretched 0.07 cm at most. Kicks at 4, 6, 8, 10 and 12 m/s into the thigh and the calf from the front and into
the calf from the side (15 runs): no tunnelling, 2 to 20 rad/s, and the impulse does not grow with the speed
(24 to 74 N*s): what a kick delivers is the leg's push while the foot is on the body more than the foot's
momentum, as with the hands. With the cap at 900 N the kick at the calf from the side had the ankle at 50 rad/s
at 8 m/s and at 5 and 12 rad/s at 6 and 4 m/s: a figure that jumps as the speed changes smoothly; at 1800 N
it is 5 to 20. It is not in the suite; the calf is kicked from the front there.

**On the desktop the F key kicks.** A desktop session's rig has the right foot as a third effector. It rests
below the camera (`tools.kick.rest_*`: 20 cm ahead, 12 cm to the right, 100 cm down, the camera taken as
level), and F sends it in a straight line to what the camera aims at and 15 cm through it, at 8 m/s, and back
(`tools.kick.speed_mps`, `through_cm`; with nothing aimed at, 110 cm ahead). The foot is in a session's
recording as the hands are, and a replay kicks as the session did; the kept recordings, made without it, replay
as before (`replay_demo_hands` and the owner's `vr_check` keep their hashes; a fresh recording of the session
without a person has three effectors and its two replays agree, 13 of 13 comparisons). In a headset there is
no foot yet: a kick there waits for a button that is known to arrive. The session without a person
(`Tools\play.ps1 -Demo -Grab`) ends with a kick at the thighs: the character tells of a hit of 28 N*s, and the
foot delivers 140 N*s in all.

Read by another, the desktop's kick had three faults, fixed before it was called done. A kick had no limit of
reach: aimed at a wall across the room, the foot would have gone there, pushing at 1800 N on whatever stood in
its line; a kick now goes no further than `tools.kick.reach_cm` (130) from where the foot rests. The resting
foot was an unseen body with a leg's strength under the camera: with a low camera it lay in the floor and
pressed on it, and over a fallen character it would have pressed on that; at rest the foot is now held with
`tools.kick.rest_force_n` (40 N) and has its strength only while it kicks. (That strength is part of the
foot's target, and of a recording, a twelfth number a target; recordings made before have eleven and read as
full strength. Set on the effector from outside, as it first was, the replay of a session had a foot of 1800 N
where the session's had 40, and the bootstrap's record-and-replay step said so: the session's foot had brushed
the floor and the replay's had not. The foot also rests 100 cm below the camera now, not 120.) A second
reading found what the weak foot had brought with it: its target came home from a kick at 8 m/s and the foot,
weak again that same frame, would have flown on past its rest (5 kg at 8 m/s against 40 N); it keeps its
strength for `tools.kick.settle_s` (0.4 s) after a kick, and after any jump of the view, which a weak foot
crossed by drifting through whatever stood between. The session's demo now also wants the foot home within
5 cm after the kick, never having been put there. And a take that begins during a kick ends the kick before
the frame's targets are made, not after. A third reading: "after any jump of the view" was first written as
"after any frame that follows a pause", which in slow motion is every frame, so that the foot was never weak
there; a jump is now a rest that has moved further in one step than a kick goes. And a strength of nothing, in
a recording written by hand, would have been a spring without a cap; an effector keeps a hundredth of its
strength at the least. And the demo's "the kick landed"
was true if the foot had touched anything since the session began; it now wants an impulse from the foot after
the key and a hit told by the character. The three standing kicks of the suite turned their targets at the
body's surface, where a body a few centimetres further back is touched by a foot already on its way home; they
go 10 cm in now.

### M4 part 4, a first step: a thing with mass in a scenario (the night of 24 to 25 November)

**Note of 26 November 2026:** Superseded in part on 25 November 2026: `reactions.thing_support` is a rule by the thing's weight and upward speed and is on ("M4 part 4: support from things…"); a held thing has a blow (`strike_held` 1, `strike_hold_scale` 5); a room's furniture can have mass.

Part 4 is objects and movable furniture. Its first need is a simulated thing in the world at all, and that is
what this is: a scenario may list `"props"`, boxes with a name, a size, a mass and a place
(`Scenarios\README.md`), and the stage puts each into the world before the first step
(`FScenarioStage::SpawnProps`): a box component that simulates, with a cube attached to be seen. It collides
with the floor, the character and the hands; a hand can take hold of it, since it is of the kind of body a
holding hand passes; the character feels it as it feels any simulated body that touches it, a hit or a
pressure. A run's `props` say where each ended, how far it went, how fast it ever was and how far it is tipped;
the expectations `min_prop_travel_cm`, `max_prop_travel_cm` and `max_prop_speed_mps` hold them. Scenarios without
props run as before (82 of 82 hashes).

| Scenario | What is done | What happens |
| --- | --- | --- |
| `prop_push_box` | a fist pushes a box of 5 kg along the floor | it slides 129 cm at 1.2 m/s at most; the hand gave it 48 N*s; the character, two paces away, notices nothing |
| `prop_lift_box` | an open hand comes down on the box, closes, lifts it 70 cm, carries it a metre, sets it down, opens | the box comes along as it was taken and stands where it is set down, 6 cm from the place; the grab's joint carries 51 N on average, the box's weight, and 157 N at most; no slip |
| `prop_drop_box_on_head` | the same hand lifts a box of 5 kg over the character's head and lets go | it falls 20 cm onto the head: one hit told (15 N*s felt), a flinch, the character stands; the box tumbles to the floor, 5.5 m/s at most |
| `prop_throw_box` | a box of 2 kg is taken, lifted, swung away from the character and let go in the swing at 5 m/s | it leaves the hand and lands 6 m away (6.9 m/s at most on its way); the joint carried 74 N at most |
| `prop_lift_heavy_one_hand` | one hand takes a crate of 40 kg by its top and its target rises 60 cm | the crate does not move; the hand is left 55 cm behind its target at its 300 N and the grip slips; nothing is thrown (the crate 0.0 m/s) |
| `prop_lift_heavy_two_hands` | the same with two hands | the crate is lifted 53 cm and set down again; each hand carries 169 N on average and 253 N at most, no slip |
| `prop_fall_on_box` | a box of 20 kg stands behind the character, which is knocked over backward onto it | it falls across the box (259 N*s felt), is 0.33 cm in the world at most, gains 25.5 J as in any fall and lies at rest after 4.0 s, where a fall on the bare floor takes 2.2; the box is moved 11 cm |

**A thing that is held keeps its direction in the hand.** A grab on a limb holds a point and lets the limb
turn; a thing held that way only hangs, and a stick cannot be struck with. So for a thing (whatever is not a
part of a character) the grab's joint also has a spring with a torque cap that turns the thing toward the way
it lay in the hand when it was taken (`effectors.hand.grab_hold_nm_per_rad` 150, `grab_hold_damping_s` 0.05,
`grab_hold_max_torque_nm` 40). The box of `prop_lift_box` swung under the hand before and ended 35 cm from
where it was set down; now 6 cm. `prop_stick_strike_arm`: a stick of 90 cm and 0.8 kg is taken from the floor
by one end, lifted to shoulder height, carried to the character's side and swung through its upper arm at
5.7 m/s. It does not pass through (0.04 cm in the world at most), and the body feels 2.5 N*s, which is not a
hit to it. Two things about that. A swing whose target leaves at full speed at once does not strike at all:
the hand is thrown sideways at 130 m/s^2, the wrist has 20 N*m to turn the stick with, and the stick trails
behind the hand and is drawn past the body lengthwise (seen on the film); the scenario's swing gathers speed
over 0.4 s. And how hard a held thing hits is the wrist's strength and the thing's mass: with the hand's turning
spring and the hold at three times their values the same stick delivers 6.7 N*s, a stick of 2 kg 4.0 N*s, both
7.0 N*s, none of them a hit. The plan's stick, whose end goes at up to 12 m/s and whose blow is a hit, needs
those values set in part 4; they are as they were.

**A handle.** The owner's rule for held objects: "a handle named in the object's spec draws the hand in when
it is near; anywhere else the object is held where it was touched". A prop may name one (`"handle_cm"`, a point
of the thing from its middle); a hand that closes within `effectors.hand.grab_handle_cm` (15) of it takes the
thing by that point, and the joint draws the handle into the hand. The hand still knows nothing of the thing: it
looks for a place with a tag on whatever it takes hold of. The stick of `prop_stick_strike_arm` names its end;
taken 10 cm from there it is held by the end (`handle_grabs` 1, the expectation `effector_handle_grabs`), and
its swing through the arm is then felt as 12.7 N*s, where held 10 cm further in it was 2.5; the body still
does not tell of a hit (the stick slides along the arm: no single frame brings what a hit is). By the engine's
source, read by the reviewer and not measured: the joint closes the distance in one substep, shared by the two
masses, and the engine's projection moves only the hand; a light stick comes most of the way to the hand, a
table of 30 kg would have the hand jump to its handle and then hang behind its target while 300 N drag the
table. Nothing is thrown.

The plan's criterion for the stick ("strikes head, chest, shoulder and forearm at several heights with its
end at up to 12 m/s: no tunnelling, and the impulse grows with the speed") is not met by this, and the sweep
says where it stands: the same stick, taken by its handle, swung at 95, 120, 140 and 162 cm at 3, 5 and 7 m/s
(12 runs; the stick itself reaches 3.7 to 8.3 m/s). It is held in all, no grip slips. The body feels 13 to
53 N*s at the three lower heights and tells of one hit in the twelve (the forearm, at 120 cm and 7 m/s); what
it feels does not grow with the speed (13.8, 29.6 and 17.8 N*s at 95 cm); at 140 cm and 7 m/s the stick is
1.53 cm in the body for a moment and only 3 N*s are felt; at the head's height it passes without touching
(the stick hangs below where the hand holds it). So: a held thing can be swung and does not go through at
these speeds, and what a blow with it is, is still to be made: the wrist's strength, the stick's mass, and how
the body counts a contact that slides along it.

A session puts the things back where the scenario has them whenever the character is put back, so that a
take's replay starts from the same places, each under the name it had (`Prop_<name>`: what a hand that touches
or holds it reports); and a run with props has a check of its own, `props_simulated`: a
limit on how far or how fast a thing went says nothing of a thing that was never made.

Known of the body and things, from the code's reading and one run: to the body every contact with a thing is
a hit or a press, never support. A trunk that comes down on a box does not count as having met the floor (the
muscles' let-go waits for the floor), a body that rests on things alone counts as held up, and a thing that
presses with 450 N or more would keep a get-up waiting. In the one case run, the body that fell across the
20 kg box got up at the first attempt in 4.5 s and felt no press. A thing that lands on a fallen body is not an
"input" to the energy watch and would read as energy from nowhere. Nothing refuses a prop that is put inside
the character or the furniture of a map.

Measured, as the starting point of part 4's step for the body (four runs, nothing changed): a body put on a
movable plate (120 by 120 by 10 cm, 40 kg) stands on it for the whole run, and its sense of balance has no
foot on the ground in any of the 720 frames: no support polygon, no margins. It tells of a hit as it lands on
the plate, and feels its own weight as a press of 799 N. Struck with 30 N*s there it stands without a step
(13.5 cm of pelvis drift, the assist at 0.15 of body weight at most), a second flinch. A box of 5 kg pushed
into its shins by a hand is one hit and a press of 282 N (366 N*s felt in all), a box of 20 kg 369 N; it stands
both, without a step. So the owner's rule for part 4, "a steady force from below is support, a sharp impulse
from the side or above is a hit", is not there in any part: every simulated thing is a hand to the body. That
step changes what the body feels and stands on, and is for a day with a baseline before it and three seeds
after.

**The step, behind a switch that is off** (`reactions.thing_support`). The owner's rule as one condition in
the place where the body takes a contact (`USandboxBodyController::OnBodyHit`): a simulated body that pushes
up on a part of the character (the impulse within 45 degrees of straight up) is treated as the floor is: it
goes to the sense of balance and is not felt. On a foot whatever pushes up is support, the landing of a step
too; on any other part only a push that is in one substep no harder than `reactions.thing_support_max_ns`
(5 N*s, which kept up is 900 N; a hand's uppercut at 6 m/s is 9 N*s and stays a hit). Anything from the side or
from above is felt as before. With the switch off every scenario keeps its hashes (93 of 93). With it on:

| On a movable plate of 40 kg | Switch off | Switch on |
| --- | --- | --- |
| standing | no foot on the ground in any frame; its own weight felt as a press of 799 N; one hit told as it lands | both feet in all 720 frames, the centre of mass 8.7 cm inside its support, the assist at 0.0052 of body weight (the floor's figure), no hit when the switch is on from the first frame |
| struck with 30 N*s | stands, 13.5 cm of pelvis drift, a second flinch | stands, 8.7 cm |
| struck with 80 N*s | falls (it has no feet to step with) | three steps, stands |
| knocked down with 200 N*s on a plate of 4 m, to get up | not run | falls, and gets up on the plate at the second attempt; its own fall and get-up are told as nine hits (5 to 36 N*s), the parts that come down on the plate harder than the limit |

Three scenarios switch it on for themselves (`prop_stand_on_plate`, `prop_step_on_plate`,
`prop_getup_on_plate`). And the plan's "a hand that lifts or presses must keep reading as it does now; that is
checked, not assumed": the whole suite run with the switch on has 90 of 95 scenarios bit for bit as they are
without it; the five that differ are the three on the plate, the fall onto the box (the body lies on it) and
one with a hand, `grab_getup_held`, where the hand that holds the chest of the lying body is under part of
it. The body cannot tell a hand from a thing, so a hand under a foot is support too. Not measured with it on:
the push matrix, the knockdowns and the stand (none has a thing in it), a thing that tips or slides away under
the feet, a body sitting or propped on a thing, a stick underfoot.

**The stand of 50 falls among movable furniture, three seeds** (the plan's criterion for part 4;
`Tools\getup-stand.ps1 -Only floor -Floor 50 -PropsFile Tools\stand_furniture_props.json`): a table of 30 kg
110 cm behind the body, a seat of 8 kg a metre to its left and a box of 3 kg at its right, all three simulated.

| Support from things | Falls that end standing | Falls in which a thing was moved | Of those: end standing | Of those: at the first attempt |
| --- | --- | --- | --- | --- |
| off (as tuned) | 50, 50, 50 of 50 | 26, 27, 29 | 26, 27, 29 | 25, 24, 26 |
| on | 50, 50, 50 of 50 | 26, 27, 29 | 26, 27, 29 | 21, 24, 25 |

Every body gets up, with the switch on and off: furniture of this weight is shoved aside by a falling body and
does not keep it down. With the switch on five more get-ups of 82 need a second attempt, which is the row of
hits a fall onto a thing is under the rule's limit (below). Heavier furniture, furniture a body gets wedged
under and furniture of a room's map are not in this.

What the reading by another and the runs showed of the rule's limits, for the owner's decision before it is
switched on:

- *A fall onto a thing is a row of hits.* The limit that keeps an uppercut a hit also makes a hit of every
  part that comes down hard on the thing; on the floor nothing of a fall is a hit. Without a limit a blow from
  below would be support. The contact alone does not tell the two apart; the thing's mass would (a body can
  lie on 20 kg, not on a hand of 1.5 kg), which is a step toward knowing what touches it.
- *The support is taken to stand still.* The capture point is worked out from the body's velocity in the
  world, and the assist's anchors are fixed in the world: a plate that slides at 0.13 m/s puts the capture
  point 4 cm out and the body steps, and a plate that carries the body 20 cm is pulled against by 300 N at the
  pelvis and at each foot. No scenario moves the plate: a hand at its 300 N against the plate's edge does not
  move it with the body on it (120 kg on the floor take about 820 N to slide), and a scenario has no force
  for a thing.
- *A hand that lifts is support.* A hand that pushes up on a body in the air counts as the world being
  touched, so the body is never "held up" (`UpdateHeld`) and keeps the pose of its fall; and an upward touch on
  a swinging foot ends the step in the air.
- *A foot that is touched from below is standing.* A body hung up by a hand whose foot something pushes up
  on no longer counts as held up, and a blow on the sole of a lying body is not felt.
- *With the grab on as well, a held get-up goes another way.* The suite with every switch of the night on at
  once (the grab, the open hand as a body, support from things) passes 93 of 95: `palm_close_on_chest`, whose
  grip closes on the chest and now takes hold of it, and `grab_getup_held`, which does not end standing in
  its 30 s: the hand that holds the chest is support to the body. The push matrix, the knockdowns and the
  stand on three seeds are the same with support from things on as off (160 of 160; 8 of 8; 49, 50 and 49 of
  50 with 38, 44 and 44 at the first attempt): none has a thing in it.
- *Things are not checked against the space a body needs as it appears* (its arms out, 58 cm to either side),
  as the stand's furniture of a room is.
- *The scenarios begin with a flinch:* a console event reaches the body after the first step, in which the
  landing on the plate was still a hit. With the switch on from the start (`-Tune`) there is none.
- The first version of `prop_getup_on_plate` passed with a body that had fallen off the plate and got up on
  the floor; no expectation says where a body stands. The plate is 4 m now, and the run was looked at.

Not done, and what part 4 proper has to decide: furniture of a room spec with mass (this is a scenario's
own list; the rooms' tables and chairs are still part of the map and static); a grip that opens under overload
by the thing's own load is the grab's own two rules (the slip and the torn grip) and was not measured on a
heavy thing; how
strong the wrist is, by which a stick's blow becomes a hit; the difference between a thing that supports the body and one that hits it, which the
body is to read from the contact; the stand of falls among movable furniture. The body's rest criterion for a
fall onto a thing is 5 s in the scenario, against the 3 s of a fall on the floor.

### M4, the blow: a hand that comes fast has the arm behind it (25 November, the owner's decision)

**Note of 26 November 2026:** Superseded in part the same day: only a fist has a blow, full from 4.5 m/s, carried 40 cm (`strike_through_cm` 40, `strike_return_cm` 15; "M4, the second look…", "The punch, how far…"); the table here is of the blow before that.

The owner tried the desktop session and found the punches and the kick weak. What he wants of a punch: into
the chest the body steps back; into the solar plexus or the belly it also doubles over for a couple of seconds
(a reaction of the body, the step after this one); a kick there fells it.

**What a punch's strength was made of.** A punch with weight behind it (15 cm into the body, 0.1 s there, back)
delivered the same 50 N*s at 4 and at 8 m/s: not the 1.5 kg of the fist, but the time the hand pressed with its
300 N while its target was inside the body. A jab (5 cm in and back at once) was 15 to 17 N*s. Heavier and
stronger hands for everything (3 kg, 600 to 900 N) give 74 to 107 N*s, but then one hand drags a lying body
(400 N) and holds a get-up down (450 N), which the grab's two rules stand on.

**The blow.** A hand that meets something after a frame without a touch, having moved faster than
`effectors.hand.strike_speed_mps` (2) as the step began, has for `strike_s` (0.15) a higher force cap and a
stiffer spring, up to `strike_force_n` (900) and `strike_stiffness_n_per_m` (9000) at `strike_full_speed_mps`
(5) and in proportion below it. The cap alone did little (50 to 66 N*s): the spring asks for its stiffness
times the gap, and a target 15 cm inside the body is 450 N at 3000 N/m. A switch at one speed was tried first
and made an edge: the push of `effector_push_chest`, which arrives at 2.5 m/s, became a blow of 43 N*s and two
steps; graded by speed it is 21 N*s and no step, as before (17). One blow at a time; a hand that holds
something strikes with nothing; the foot has no blow (its own cap is 1800 N).

| A punch (the hand's `contact_impulse_ns`, what the body did) | chest | belly | head |
| --- | --- | --- | --- |
| before, with weight, 4 and 8 m/s | 50, 3 steps | 50 to 53, 2 to 3 steps | 25, a flinch |
| the blow, with weight, 4 m/s (two thirds of it) | 77, 4 steps | 104, 3 steps | 32, 2 steps |
| the blow, with weight, 8 m/s | 83, 4 steps | 113, 4 steps | 28, 2 steps |
| before, a jab, 4 and 8 m/s | 15 to 17, a flinch | 17, a flinch | 11 to 14, a flinch |
| the blow, a jab, 4 m/s | 28, a flinch | 31, a flinch | 19, a flinch |
| the blow, a jab, 8 m/s | 24, a flinch | 23, a flinch | 14, a flinch |

A jab at 8 m/s gains little: its target is in and out of the body in 12 ms. With the values chosen no joint
of the body went faster than 21.8 rad/s in the twelve strikes (the limit is 30), none stretched by more than
0.08 cm, and the hand was at most 0.02 cm inside the body. With 1200 and 1500 N the belly takes 124 to 136 N*s
and up to seven steps, and a joint reaches 25 rad/s; 900 N is the row for what the owner chose ("the body
steps", the 80 N*s class of the M3 table).

**What it changed.** Of the 99 scenarios 80 keep their hashes; the 19 that changed are those in which a hand
arrives at 2 m/s or faster: the punches and slaps (`strike_punch_*` 6, `strike_slap_*` 3, `palm_slap_*` 3,
`palm_open_close`, `effector_hit_head`), where the impulse grows on the trunk and the legs (torso 29 to 43,
leg 27 to 38, slap on the leg 29 to 43, slap on the trunk 14 to 20 N*s) and stays what it was on the head, the
shoulder and the forearm (13, 7 and 10 N*s; why a blow adds nothing there was not looked into); the two pushes that arrive at 2.0 and
2.5 m/s (`effector_push_chest` 17 to 21 N*s, `effector_push_chest_steps` three steps to five); the two get-ups
with hands sent at the lying body (`getup_held`, `grab_getup_held`: the same story, other figures); and
`replay_demo_hands`. One expectation was rewritten: `strike_punch_torso` wanted "a flinch and nothing else" and
now wants one to four steps and a body that stands (`min_steps`, a new expectation, and `effector_strikes`,
the number of blows). New: `strike_punch_belly_steps` (111 N*s, four steps), `effector_push_slow_no_blow` (a
hand that arrives at 1.3 m/s and pushes is no blow).

**Measured again, because written claims rest on the hands:** the grab's pulls 93 of 93 (the joint 294 N on
average at most); one hand slows a get-up and two hold it, to the figure (from one hand 77 grips of 146 come
off and 37 bodies stand within the 15 s, from two none; 144 of 150 stand in the end with either).

**The desktop hand** goes at 8 m/s (`tools.hands.reach_speed_mps`, from 3) to what the camera aims at and
`tools.hands.through_cm` (15) through it. A click is a punch; a button held is a hand that goes on through the
body, which fells it. With nothing under the crosshair the reach is what it was (the wheel). An open hand (the
G key) reaches as before, as far as the wheel has set, at `tools.hands.open_reach_speed_mps` (2): no blow when
it arrives. An open hand that stopped 3 cm past the surface was tried and taken back: the body gives way by
more than that, out of a grab's reach, and the session of three takes then differed from its replays by 7% of
the hand's impulse (half a centimetre of the body's stance is a sixth of a push that is 3 cm deep). That
session's touch is an open hand now; as a fist it had become a punch with steps (the bootstrap's step caught
it: pelvis 7 and 12 cm apart).

Open: in the headset a blow's strength comes from the speed of the owner's own hand, which no scenario has.

### M4, doubling over, and the kick's strength (25 November, the owner's decisions)

**The rule (the owner's choice of three).** A hit from the front into the belly or the solar plexus that
brings 40 N*s or more makes an upright body double over for about two seconds: a jab does not, a punch with
weight behind it and a kick do. A hit into the chest does not.

**How the body knows.** The frame's largest contact has a point (`FeelImpulse`); it is into the belly when it
is on the trunk, pushes the trunk backward (against the trunk's own forward, which is across the line of the
hips and the line from the pelvis's joint to the neck's) and lies between two shares of that line
(`reactions.double_over_from` -0.15 and `double_over_to` 0.53: from the groin, 82 cm on a standing body, to
just under the chest's centre, 119 cm). What such frames bring beyond the pressure felt already is summed over
one contact (a new one after `thresholds.flinch_window_s` without a touch there); when the sum reaches
`reactions.double_over_ns` (40) and the body is Standing, in a Flinch or in a Stagger, it doubles over: once a
contact. `poses.double_over` (the trunk 25 and 25 degrees forward, the head 20 down, the upper arms turned in
and the forearms bent across the belly) comes in over `double_over_in_s` (0.2), is held until `double_over_s`
(2.0) after the hit and goes out over `double_over_out_s` (0.8), by its own clock: it stays on a body that
staggers, steps or falls, and a body that lies or gets up lets it go. Another such hit while it is on holds it
for two seconds more. A body that falls, lies or gets up does not double over. The body tells its listeners
(`double_over` in `control.events`; `control.double_overs`, `control.max_belly_hit_ns`; the expectation
`double_overs`).

**Where the threshold stands** (what one contact brings into the belly, by the body's own sum, N*s):

| | belly sum | doubles over |
| --- | --- | --- |
| a punch with weight at 97, 105, 117 cm, and 8 cm to a side | 53 to 64 | yes |
| a jab at 105 cm, 4 and 8 m/s | 22 to 26 | no |
| a hand that pushes the belly (5 and 15 cm deep, 1.3 and 1.5 m/s) | 24 to 28 | no |
| a punch into the chest at 128 cm | 0 | no |
| a punch into the back at 105 cm | 0 | no |
| a kick into the belly | 80 to 205 | yes |
| a kick into the thigh or the calf | 0 | no |

The body's sum is about half of what the hand delivers (63 of 112 N*s): what the hand goes on pressing with is
pressure to the body, not hit. The zone's edges were moved twice by what the measurements showed. Its lower
edge was at 95 cm at first: a punch at 97 cm then summed 37 N*s, because half its frames touched below the
edge, and a kick that rises into the belly 12 to 15. Its upper edge was at 122 cm: a scripted impulse is felt at
its body's centre of mass, the chest's is at 121 cm, and every "blow on the chest" of 80 and 200 N*s from the
front doubled the body over: 20 scenarios changed, among them eight get-ups. With the edge at 119 cm three
changed, the three in which the belly is struck.

**What it changed.** The push matrix: 160 of 160, every run ends as it did. The knockdowns: 8 of 8. The stand
on three seeds: 49, 50 and 49 of 50 with 38, 44 and 44 at the first attempt, the figures of before. The suite:
96 of 101 keep their hashes. Changed by the doubling: `strike_punch_belly_steps` (still two to six steps and
standing; doubled over once). Changed by the foot (below): the four `strike_kick_*`.

**The kick.** With the foot of the night (5 kg, 1800 N) a kick into the belly was 96 N*s, no more than a
punch, and the body took two steps. Six feet were tried on six kicks (36 runs, the body able to double over):

| the foot | into the belly | with 0.1 s there | 10 cm deeper | from 40 cm to a side | thigh | calf |
| --- | --- | --- | --- | --- | --- | --- |
| 5 kg, 1800 N | 96, 2 steps | 129, 4 steps | 116, 5 steps | 81, 4 steps | 80, 3 steps | 51, a flinch |
| 8 kg, 2500 N | 134, 3 steps | 150, falls | 159, falls | 113, 7 steps | 111, 4 steps | 65, 2 steps |
| 8 kg, 3500 N | 170, falls | 175, falls | 204, falls | 143, 5 steps | 143, 5 steps | 72, 5 steps |
| **10 kg, 3500 N** | 179, falls | 181, falls | 211, falls | 146, 3 steps | 142, 3 steps | 73, 5 steps |
| 10 kg, 4500 N | 210, falls | 210, falls | 256, falls | 175, falls | 174, 3 steps | 78, 4 steps |
| 12 kg, 4000 N | 197, falls | 197, falls | 235, falls | 163, 7 steps | 160, 3 steps | 75, 5 steps |

Taken: 10 kg, 3500 N, 24000 N/m, one row past the first that fells with all three straight kicks. No joint
goes faster than 23.4 rad/s with it (the limit is 30; 31.1 once with 4500 N), none stretches more than 0.11 cm.
It is the foot's strength that fells, not the folding: with the doubling switched off
(`reactions.double_over_ns=0`) the three straight kicks of this foot fell the body as well (179, 181 and
211 N*s, after three to four steps). (The night's "8 kg and 3500 N: 166 N*s, seven steps" was a softer
spring, 16000 N/m against 24000, and is no comparison.) (Noted on 26 November 2026: this figure is not in the night's section, which says 8 kg and 3000 N fell the body; its source is unverifiable.) `strike_kick_torso` (10 cm into the
belly and back: 130 N*s) wanted a body that stands and now wants one that falls, doubled over, by the fall's
criteria; the kick into the thigh is 108 N*s and six steps, into the calf 75 N*s and five.

Looked at (`Saved\Films\strike_punch_belly_steps`, `strike_kick_torso`): punched, the body steps back, folds
with its forearms across its belly and its head down, stands so for two seconds in the stance its steps left it
in, straightens and brings its feet together; kicked, it folds, sits down backward and rolls onto its back with
its knees up and its hands at its belly, then lets go.

Open: the doubled-over body stands as well as any other (its legs are not weaker); a blow into the belly of a
body that is getting up does not double it over; nothing of this has been seen in the headset.

### M4, the desktop's kick as a thrust, and the knockdown by a hit on the head (25 November, after the owner's second look)

The owner tried the punch (good: "looks like a punch") and the kick: weak. What he wants of a kick: straight
into the head, the head is thrown back and the body falls; into the belly, the body doubles over and falls.

**Why the desktop's kick was weak where the scenarios' was not.** The desktop's foot went in one line from
where it rests, a metre under the camera, to the point aimed at: into a belly at 54 degrees upward, into a head
at 65. The night's scenarios kicked at 28 degrees. A kick is now a thrust: the foot comes up to the height of
the point `tools.kick.chamber_cm` (50) before it and goes level from there, the way the camera looks, to
`tools.kick.through_cm` (15) through it, and back the same way; a leg reaches 150 cm from where the foot rests
(`tools.kick.reach_cm`, from 130: a head was just out of it). With the foot of 10 kg and 3500 N, kicked from a
rest at (12, 75, 60) cm:

| | level thrust | one line from the rest (as it was) |
| --- | --- | --- |
| head | 54 N*s, five steps | 39 N*s, three steps |
| chest | 138 N*s, falls | 93 N*s, falls |
| belly | 156 N*s, doubles over and falls | 145 N*s, doubles over and falls |
| thigh | 157 N*s, four steps | 169 N*s, three steps |

**The head does not fall to momentum.** Thrust 30 cm through instead of 15, the kick felled the body from three
points of nine on the head, 6 to 8 cm apart (68 to 108 N*s), and not from the other six (41 to 73 N*s; from a
side and from behind: three steps): the head is a small round thing on a soft neck and the foot slides off it. A figure
that changes with the centimetre is a scenario on an edge; depth was left at 15 cm.

**The knockdown** (`reactions.knockdown_head_ns`, 35). What one contact brings onto the head, by the body's own
sum as for the belly, takes the legs from under an upright body when it reaches 35 N*s: the body goes into
Falling at once, falls by the fall's own pose and muscles, lies, and gets up as after any fall. From any side.
A body that falls, lies or gets up is not knocked down again. Where the threshold stands:

| on the head | the body's sum, N*s | knocked down |
| --- | --- | --- |
| a jab, 4 and 8 m/s | 14 to 17 | no |
| a punch with weight at three heights, a hook from the side | 20 to 27 | no |
| a scripted blow of 30 N*s (the M3 table's row) | 30 | no |
| a kick at nine points of the head, from a side, from behind, 15 cm deep | 35 to 76 | yes, all twelve |

One kick of the twelve, a glancing one on the crown, is at 35.4: either way would do for it.

**What it changed.** The suite: the 101 scenarios of before keep their hashes (no scenario brought 35 N*s onto
a head; the desktop's kick is in no scenario); new `strike_kick_head_knockdown` (the head thrown back, the body
on its back half a second later; `Saved\Films\strike_kick_head_knockdown`), and the expectation `knockdowns`.
**The push matrix: 160 of 160 by the owner's M3 rules, but six cells of the 80 N*s row end otherwise: a blow of
80 N*s on the head now fells the body where it stepped (stepped 35 and fell 2 became stepped 29 and fell 8).**
The knockdowns of the fall's criteria: 8 of 8, unchanged. The stand on three seeds: 50, 50 and 49 of 50 with
43, 45 and 44 at the first attempt (49, 50, 49 with 38, 44, 44): those of its falls that begin with a blow on
the head are other falls now.

Open: a hand's hit on the head in the headset can reach 35 N*s if it stays on the head (a punch here brings
27 at most); a kick has no source in the headset; a knocked-down body gets up as fast as any other.

### M4, the second look in the headset, the fist's carried blow and the open hand as a body (25 November)

**Note of 26 November 2026:** `strike_through_cm` is 40 since the owner's decision later that day ("The punch, how far a fist is carried…").

**The session** (`Saved\Recordings\vr_check2`, 180 s, with the grab on; the owner's first with the blow, the
doubling over and the knockdown): 89.9 frames a second, both controllers tracked in every one of 15845 frames,
no error, and the replay ends as the session did (pelvis 0.00 cm apart). Every key of both controllers reached
the player's input, the triggers among them, which had not arrived the day before (touch, click and axis of
trigger and grip, the sticks, X and Y, A and B). The hands took hold 17 times and held for 26 s in all; no grip
slipped. 51 hits: 31 on the head, 9 on the chest, none into the belly (the belly's largest sum 16 N*s, the head's
29); the fist arrived at 2 to 5.6 m/s and a contact brought 8 to 34 N*s on the head and up to 44 on the chest;
36 blows; 69 steps, three falls, no doubling over, no knockdown. No new
take was made.

**What the owner said:** the punch is weak, about as a slap; an open hand is a ball and a rectangle, and the
body answers the ball only; the body answers a fist and an open hand alike, and a fist has to be stronger.

**Why.** (1) `effectors.hand.palm_collides` was off: the open hand's plate was drawn and the ball collided.
(2) A blow was given to every hand that came fast, open or closed. (3) A player's own hand stops where it sees
the body's surface and is on its way back a frame later; a scripted punch "with weight behind it" has its
target 15 cm inside the body for a tenth of a second. The same hand delivers 8 to 34 N*s the one way and 77 to
113 the other, and his right hand was open for three quarters of the session.

**What was changed.**
- `effectors.hand.palm_collides` is 1: an open hand is the plate, as a body.
- Only a fist has a blow (`bFist`, and the ball as the body).
- A blow is carried through (`effectors.hand.strike_through_cm`, 25): while it lasts the fist is asked to be at
  least that far beyond where it landed, the way it came, as much of it as the blow has of its strength; a
  target that stops short or goes back is moved up to that along the blow's way and loses its speed backward.
  What a blow delivers is then set by the speed the fist came at and not by what the player's hand does after
  it has landed. Its whole strength is from 4.5 m/s (`strike_full_speed_mps`, from 5).

Two ways of it were tried and taken back, each by a measurement. A place beyond the *target* did nothing for a
jab (11, 11 and 11 N*s with 0, 15 and 25 cm): the target is on its way back and takes the place with it, and
its speed backward brakes the fist through the spring's damping. A place beyond where it *landed*, held
whatever the target did, made `effector_push_chest_steps` fall: a hand that came at 2.0 m/s to push stood at
the surface for the blow's time, and then had its whole push at once.

**Punches as a person in a headset throws them** (the target stops at the surface and goes back at once;
4 cm deeper gives the same figures):

| the fist comes at | chest | belly | head |
| --- | --- | --- | --- |
| 3 m/s | 33 N*s, two steps | 39 N*s, two steps | 13 N*s, a flinch |
| 4 m/s | 70 N*s, three steps | 86 N*s, four steps, doubles over | 27 N*s, two steps |
| 5.5 m/s | 86 N*s, four steps | 110 N*s, doubles over | 34 N*s, two steps |
| an open hand at 4 m/s | 7 N*s, a flinch | 9 N*s, a flinch | no touch at that depth |

No joint faster than 23.3 rad/s, the hand no deeper than 0.12 cm in the body. A punch whose target goes 15 cm
in and stays 0.1 s is now what a jab at its speed is (70 and 90 N*s at 4 m/s, where it was 77 and 104); with
its whole strength, from 4.5 m/s, it is the 86 and 110 N*s here against the 83 and 113 the owner looked at on
the desktop.

**A recording replayed with other hands is no measurement of those hands.** The owner's recording replayed with
the carried blow had 9 hits where the session had 51 and ended a metre from where the session did: after the
first hit that lands otherwise the body stands elsewhere, and what the hands were asked for goes into the air.

**What it changed.** 69 of 102 scenarios keep their hashes. The 33: every scenario with a fist that arrives
fast, with an open hand (it is a plate now: the grabs, the things that are taken, the slaps), or both. Ten
expectations were rewritten, all for the same reason, "a flinch and nothing else" under a fist that now makes
the body step: `effector_hit_head` (35 N*s, two steps), `strike_punch_head` (36, three), `strike_punch_forearm`
(80, six), `strike_punch_leg` (156, four), `strike_punch_torso` (101, five, doubles over), `palm_open_close`
(88, four) want a body that stands again, within a number of steps; the three `strike_slap_*` are open hands
now, as a slap is, and no blow; `strike_punch_forearm_heights`, which holds the wrist against a fist at speed
at several heights, is struck with the blow switched off (the body stepped away after the first of its four
punches), and gives the hashes it had before there was a blow: without one a fist is what it was.

**Measured again.** The grab's pulls: 93 of 93 (the joint 294 N on average at most, 389 N in a frame). The
stand with holding hands: from one hand 89 grips of 146 come off and 47 bodies pull free and stand within the
15 s (77 and 37 with the ball as the open hand), from two none; 143 and 144 of 150 stand in the end.

**The probe for tunnelling was wrong for a plate.** It held the plate's middle to half the plate's thickness
from any surface, whichever way the plate lay; two pulls of 93 (the calf, downward) failed on one frame in
which the released plate was 2.07 cm deep in the foot, not by its flat side. The probe now takes how far the
shape reaches from its middle the way the overlap would be left (`ReachCm`). The setting first blamed,
`effectors.hand.grab_clear_cm`, was tried at 1, 0.5 and 0.3 with the same two failures to the digit: when a
figure does not move with the setting that should move it, the cause is elsewhere.

**The third session in the headset, the same day** (`Saved\Recordings\vr_check3`, 180 s, the grab on, with these
hands; the session's own log is kept beside the recording as `session_run.log`). By that log: 31 hits; the body
doubled over four times (hits of 40 to 44 N*s into the belly by its own sum) and fell eight times (four from
punches, one knocked down by a fist on the head that brought 35.1 N*s, one at the end, two get-ups that were
given up under the owner's hands); no new take was made. Both controllers tracked in every frame, no error. The
grip closed 44 times.

**The owner's answers.** The punch is good. The open hand lies as his palm does and the body answers it. A very
hard punch on the head may knock the body down as that one did: the threshold stays at 35. A hand out of the
headset's sight behaved (it stayed and caught up). The vibration is felt. The take's gesture he did not try, in
either session of the day: it is still unseen in the headset.

**A session with hard blows is not reproduced by its replay.** `vr_check3` replayed ends 153 cm from where the
session did, and no fall of the eight happens in it; `vr_check2`, of the same morning with weak punches,
replayed to 0.00 cm. A session and its replay are two runs that differ by a frame after the first contact
(M4 part 1, step 1); a touch keeps that within limits, a blow that makes the body step does not. So
`Tools\recording-report.ps1`, which reads a replay, says what the hands were asked for and not what happened
in such a session: for that, the session's own log.

Open: the take's gesture in the headset; no kick in the headset; a report on a session from the session's own
figures, not from a replay.

**The grab is on by default** (`effectors.hand.grab` 1; the owner's word of 25 November, after the desktop
session and the two in the headset: 17 grabs in the first of those, no grip slipped; the second is known by
its log only, which does not tell of grabs). Of the 102 scenarios 101 keep their
hashes: a grab is taken only by a grip that closes within reach of a body, and a hand that is a fist from its
first key closes on nothing. The one that changed, `palm_close_on_chest`, holds what an open hand does when it
closes on a body with no grab (it stays the plate until a fist has room, and the grip is counted); with the
grab it took hold of the chest. It switches the grab off for itself and has the hashes it had.

### The machine's reboot of 24 November 2026, 22:03

The fifth launch in the headset that evening ended in a blue screen and a reboot. What the logs say:

| Source | Finding |
| --- | --- |
| System log | `Kernel-Power` 41 at 22:03:45; bugcheck `0x0000003b` (SYSTEM_SERVICE_EXCEPTION, access violation `0xc0000005`), "possibly related driver: nvlddmkm.sys"; the first bugcheck in 60 days |
| SteamVR's logs | The owner shut SteamVR down at 21:50:28. At 22:03:26 `vrserver` is starting again: the launch's own OpenXR instance started it |
| The engine's log of that launch | `xrCreateInstance` at 22:03:24 to :26, then the D3D12 device: the last lines are "Aftermath initialized" and "Emitting draw events for PIX profiling" at 22:03:27. In a run that goes on, "Aftermath enabled" and the device's interfaces follow within 60 ms |
| Machine | RTX 5080, driver 32.0.16.2114 (621.14) of 27 October 2026; Windows 11 build 26300.9974; hardware-accelerated GPU scheduling on |

So the graphics driver failed in the kernel while the engine created its device and SteamVR's compositor was
taking the headset's display at the same moment. The four launches before it found SteamVR running and ready.
The dump could not be read (no administrator's rights in this session), so the driver's function is not named.

What was done: `Tools\play.ps1 -VR` and `Tools\scenario.ps1 -VR` refuse to start unless SteamVR's server and
compositor are running, and `Invoke-Editor -VR` refuses as a last guard; a run in the headset is started only
on the owner's word that the headset is on; in unattended work what is not committed is copied to
`Saved\Backups` before work goes on. After the reboot: the 27 changed files intact, `git fsck` clean, build,
33 unit tests, suite 65 of 65 with the baseline's hashes.

What only the owner can do (administrator): read the dump (`C:\Windows\Minidump\112426-16843-01.dmp`) for the
driver's function; update the NVIDIA driver; consider switching hardware-accelerated GPU scheduling off for the
headset. None of it is needed for the work to go on.

### The natural body, N1: the measures and the first scorecard, 2026-11-25

N1 of `docs/natural-body.md`: measures of how natural the motion is, in every run, and a scorecard
(`Tools\natural.ps1`). No behaviour changed. What each figure means is in `Scenarios/README.md`, "How natural the
motion is".

**Nothing moved.** Against the baseline `pre_n1`, taken before the first edit: the suite's 101 scenarios with the same
two hashes (checked after every step), the push matrix's 160 runs with the same ends, the eight knockdowns with the same
hashes, the stand on three seeds with the same 50, 50 and 49 of 50. The grab's 93 pulls were run before the change only:
what they run on is the suite's code with the suite's hashes. The suite takes 43 s with 10 scenarios more
(40.8 s before), 45 allowed. Unit tests 41, six of them new; the bootstrap passes.

**What each measure was checked against.** The answer was worked out beforehand, from the scenario's own numbers or the
run's raw trace, and each check was seen to fail with the measure broken on purpose (22 breaks, all caught; one, the
database mirrored without changing sides, only after a check had been added for it).

| Measure | Check | Answer | Reads | Broken on purpose |
| --- | --- | --- | --- | --- |
| How a fall meets the floor | `measure_drop_head_first`: limp, upside down, 1.02 m of free fall | the head at 4.47 m/s | 4.45 | speed after the step: 0.78; the spine taken for the head: 0.26 |
| Reaction timing | `measure_reaction`: the body's own answers off, a pose asked for 0.3 s after the hit | 0.30 to 0.33 s, the elbow; strength at once | 0.322; 0 | another target at the hit: 0.00; the hit's frame wrong: 0.10 for the strength |
| Joint effort | `measure_arm_held_out`: the arm's weight about the shoulder, from masses and places in the trace | 9.9 N*m | 9.6 | a factor of 1.5: 14.4 |
| Time at the cap | `measure_arm_at_cap`: a cap of 4 N*m under 9.9 | all the time | 1.0 | the cap not looked at: 0 |
| Compliance | `measure_push_neck`: 20 N on the head, chest welded, one spring of 40 N*m/rad | 1.29 cm, 15.6 N/cm | 1.30; 15.4 | the force of one frame: 0.17; another axis: 398 |
| Jumps | `measure_jumps`: an elbow's target moved by 30 degrees at 40 N*m/rad, then a quarter of the strength | 30.0 degrees, 20.9 N*m, 75% | 30.0; 20.94; 75% | against another pose: 179; of another strength: 99.9% |
| The head | `measure_free_fall` | 9.8 m/s2, no jerk | 9.79; 0.1 m/s3 | no time step: 0.11 |
| Stillness | `measure_still` (held by the pelvis, no idle), `measure_stand` (with the idle) | still to the end; never | 7.2 s of 8; 0 | a threshold 5000 times smaller, 100 times larger: 0; 32 s |
| Beyond a joint's range | a unit test; `measure_stand` | 10 frames of 100; 0 | 0.10; 0 | outside from the middle on: 1.0 |
| Centre of pressure | `measure_cop`: nothing but the floor holds the body | under the centre of mass (0.1 cm); its weight | 0.005 and 0.063 cm off; 0.995 | unweighted: 11 cm off; one phase of the solver: 0.003 of the weight |
| Period | `measure_stand`: the idle clip of 7.57 s; unit tests (a loop, a drift, noise) | 7.57 s | 7.60 (a point every 0.067 s) | none found |
| Likeness of the pose | `pose-prior.ps1 -Build`: each pose found at 0, mirrored twice itself, each side's angles on the other; a clip of the database asked of the body; a pose no one takes | 0; the muscles' error from the clip (3.4 degrees); far | 0; 3.2; 28.8 | joints not compared; mirrored without changing sides: refused |

**Four things the checks found.**

- **A contact event has one point of the contact, not where its force goes through.** The centre of pressure made from
  the events' points lay 8.6 cm behind the centre of mass. It is the nearest point of the solver's manifold
  (`CalculateWorldContactLocation`) with the impulse of all of them. The measure now reads the manifold's points with
  their own impulses, after every substep. N2's plan to take the place of the floor's push from the events would have
  been 8.6 cm wrong (its torques by 8.6 cm times the body's weight, 67 N*m).
- **The solver's joint projection and unfinished iterations move the bodies after the drives, by 0.2 to 0.35 degrees.**
  The spring law read the arm's 10.05 N*m at the shoulder as 9.56 with a stiffness of 100 N*m/rad, 8.97 with 300, 7.87
  with 600 and 5.94 with 1200, and the collarbone's 16.9 as 20.5. With 200 position iterations and no projection it read
  10.01 and 17.05: the law is right, and a torque from it is wrong by about 0.005 rad times its stiffness. (Projection
  is also why the floor's push on a standing body adds up to 0.995 of its weight; without it 0.9999.)
- **A measure of naturalness can be met by something that is not natural.** C14 (a hand or a forearm before the trunk) reads
  100% of the falls by hand forward and sideways. On the dense strip (`Saved\Films\n1_hand_090`) the hands do come
  first, 0.28 s into the fall, and land beside the hips while the head strikes the floor at 4.7 m/s. **The owner's rule of
  the same day: a hand counts only if it lands ahead of the chest along the way the body goes** (`hands_ahead_first`;
  `AheadOfChest` is its unit test). By it the share is 86% of 7 by hand (the fall on the strip is the one that does not
  count) and 80% of 5 by blows: still within the proposed 80%. Hands that land 10 to 20 cm ahead of the chest do not brake
  anything either; in those seven falls a hand lies 19 cm ahead at most, and the head strikes the floor in most of them.
  C15 and C16 are what N6 will move.
- **The body leans past where it stood, into a push that is held.** Under 20 N from the front the chest gives 1.3 cm and
  is 5.0 cm against the push 1.5 s later. That is the lean of M3 part 3 (`balance.lean_*`), which moves the body until the
  push's moment is gone; a person under 20 N moves hardly at all.

**What cannot be relied on.** The centre of pressure's side-to-side mean is 0.3 cm off the centre of mass's while the idle
moves the body (0.06 without it), through every variant of solver iterations and projection tried; 0.3 cm of body weight is
2.4 N*m of roll that the bodies' momenta do not account for. The cause was not found; C22's side-to-side spread is 4.8 mm
and holds that much error. `command_step_max_nm` takes a target that moves fast for one that jumps: the swinging leg's, in
a recovery step. `power_peak_w` in the thousands is the spring law on joints whipped around by a blow. And the pose
database knows no pose of a body falling freely: 21 degrees there against 11 standing is as much its lack as the fall's.

**The scorecard "before the track"** (`Saved\Natural\before_track`, pose database 2a44916b: 13 564 poses of 224 clips).
Of 26 criteria 3 are within the proposed limits, 14 outside, 9 not measured before their part or not judged.

| # | The body today | People | Proposed limit | |
| --- | --- | --- | --- | --- |
| C1 | Standing 0.0062 of body weight and 3.1 N*m on average; up to 0.18 of body weight in a flinch; no force and up to 328 N*m (pelvis, chest and head added up) in a stagger | nothing | 0 | out |
| C2 | 0.27 of body weight | nothing | 0.10 | out |
| C3 | 2.5 (800 N*m/rad a leg; m g h is 637 N*m, not the 785 the plan was written with) | 0.64 to 0.91 | 0.6 to 1.0 | out |
| C4 | Wrist 40, neck 40 N*m/rad | 3 to 4; 24 | 10; 20 to 30 | out |
| C5 | 4 of 8: ankle 250 against 112, hip 250 against 178, elbow 80 against 51, wrist 80 against 25 N*m | the table | 0 | out |
| C6 | No tone yet. 20 N from the front: chest 1.3 cm (15 N/cm), forearm 8.9 cm (2.3 N/cm) | wrist 5 to 20 times | 2 | - |
| C7 | 1.6% at most (ankle, 80 N*s to the left); 0% standing | - | 5% | in |
| C8 | Strength changes in the first step after the hit is felt, a target one frame later | trunk 106, head 142 ms | `react_passive_first` | - |
| C9 | 0.011 s, all 8 hits | 0.07 to 0.11 s | the same | out |
| C10 | Lifts at 0.033 s, down by 0.30 s (4 of 8 stepped) | 0.24 to 0.33; 0.44 to 0.58 | from 0.20; by 0.58 | out |
| C11 to C13 | No perception | | | - |
| C14 | Ahead of the chest: by hand 86% of 7 falls forward and sideways, by blows 80% of 5 (wherever it lands 100% and 80%; see above) | 98% | 80% | in |
| C15 | Strikes it at 0.5 m/s or more (the owner's rule of 25 November): by hand 67% of 12, by blows 50% of 8; touches it at all: 83% and 75% | 11% | 15% | out |
| C16 | By hand 4.5 m/s, by blows 2.8 m/s (90th percentile) | 1.7 soft, 3.2 hard | 2.0; 3.2 | out |
| C17 | Rest after 2.2 s, 25.5 J, 1.2 cm | - | 3; 40; 2 | in |
| C18 | 5.0 s at most, 3.0 s in the median of 20 falls (runs of 10 s) | breathe 12 to 20 a minute | 1.0 s | out |
| C19 | One pose asked for 3.6 s at most, the body still for 2.3 s; 24% of 199 attempts given up | - | 1.0 s; 10% | out |
| C20 | 98 to 100% stand; 43, 45, 44 of 50 at the first attempt (the plan's 38 to 44 was M3's gate) | - | 95%; 45 | out |
| C21 | 4.7 s in the median of 78 (3.2 to 6.4), first move of the first attempt to standing | 2.6 to 2.9; 4.2 | 2.6 to 4.2 | out |
| C22 | 12.9 mm/s; side to side 4.8 mm; period 7.6 s | 9.2 +- 1.6; 4.0 +- 1.3 | 6 to 12; 2 to 6; none | out |
| C23, C24 | No variation | | | - |
| C25 | 127 degrees of an upper arm's target as a get-up begins, with its muscle at no strength; otherwise 28 degrees of the thigh in the frame after a step's lift; 399 N*m asked of an ankle as a step lands. Standing 1.5 N*m | - | these, less the defects | - |
| C26 | The get-up has its three scenarios | - | each phase its three | - |

Beside the criteria, distance to the nearest recorded pose in degrees (mean, 95th percentile, worst), time beyond the
joints' ranges, the head's largest acceleration:

| Set | Runs | Pose | Beyond range | Head |
| --- | --- | --- | --- | --- |
| Standing 60 s | 1 | 11.1, 11.4, 11.5 | 0 | 0 m/s2 |
| 30 and 80 N*s on the chest, four sides | 8 | 10.8, 13.9, 14.1 | 0.08% | 63 |
| Strikes | 5 | 11.3, 17.7, 18.8 | 0.16% | 38 |
| Blows of 200 N*s | 8 | 21.1, 25.5, 26.4 | 0.04% | 149 |
| Two fists at 4 m/s, sixteen sides | 16 | 18.8, 26.6, 26.7 | 0.14% | 153 |
| Get-ups | 4 | 11.0, 24.8, 26.0 | 0.64% | 149 |

A shove of two fists at 4 m/s fells the body on 12 of 16 ways; straight sideways and 22.5 degrees forward of that, on
either side, it is stepped off. (At 2 m/s, 4 of 8.)

**The independent reading of the diff** (by one who had the code and not the account of it) found the simulation clean and
twelve defects in the figures and the scorecard, all fixed before the report above: C1's standing force was never judged
(a PowerShell comma binds to its first item only), missing data read as 0 and "in", a state's stillness counted time from
before the state began, a reference was used after its array had moved, the physics step's clock ran over the new measure,
the head's impact was the first touch only, sway joined stretches of standing at two places, the database's stamp did not
cover its poses, and smaller ones.

### M4 part 1, step 4: the owner's two sessions in the headset (25 November)

**Note of 26 November 2026:** Superseded in part: the fist is carried 40 cm ("The punch, how far…"); what held the slapped head back was the world's hold on the head, not the neck ("N8 A…"; the head is a body of its own and the default since 26 November); the get-up is led since 26 November ("R1: the led get-up").

`Saved\Recordings\vr_step4` and `vr_step4_2` to `_4`: four takes, 175 s; then, the owner having chosen a second
session, `vr_step4b` and `vr_step4b_2` to `_6`: six takes, 287 s. 462 s in all of the 600 planned. What the owner saw is below. The first session:

- **The headset.** 89.9 frames a second (89.0 in the first ten seconds). Both controllers tracked in all 14 795 frames. The target caught up with
  its controller one to three times a hand (24 to 75 frames).
- **The gesture of a new take** worked three times (its first trial): the character put back, a recording of its own. Whether it was
  meant each time is not known from the recording: the fists were at 184 to 197 cm.
- **From the session's own log:** two falls (pelvis tilted by 35 degrees at 44.2 s of take 1; a knockdown by a hit on the head), two doublings
  over (the figures the log has with them, 35.2, 42.9 and 42.4 N*s, are the running sum as it passed the threshold of 35 or 40,
  not the hit's size), two get-ups at the first
  attempt (5.6 and 5.0 s), one of them hit with 5.3 N*s while it went on.
- **From the replays** (`Tools\recording-report.ps1`): 21 hits, 17 grabs, one slipped, none torn open; no tunnelling, 0.13 cm inside a
  body at most. Takes 2 and 4 end as the session did (2.4 and 0.1 cm), takes 1 and 3 do not (61 and 187 cm): as with every
  session of hard blows and grabs, so their figures are the replays' own. The two fastest fists of take 3: 4.3 m/s 77 N*s and
  6.5 m/s 38 N*s on the chest.
- **The hand against its target** (`Tools\swing-lag.ps1 -Recording ... -FromS 33 -ToS 42`, nothing touched): 0.6 cm under 0.5 m/s, 1.3
  to 1.8 up to 2, 4.8 at 2 to 3, 6.3 above 3, with the difference of two poses; with the runtime's velocity 5 to 11 cm up to 3 m/s.
- **Little of it:** two blows above 4 m/s.

The second session:

- **The headset.** 89.1 to 89.9 frames a second; both controllers tracked in all 25 863 frames, no loss. The gesture of a new take
  worked five times more (eight in all).
- **From the session's own log:** three knockdowns by a hit on the head, four falls by the posture, six doublings over (eight over
  both sessions). Over both sessions twelve get-ups were begun and nine ended standing; three were given up, each with hits felt
  while it went on (the pelvis 40 cm from where the clip has it). Twice the weight did not come over the stance foot within 2 s.
  One body that had just got up fell again 0.6 s later.
- **From the replays:** 54 hits, 27 grabs, two slipped, none torn open; no tunnelling, 0.16 cm inside a body at most; the hands that arrived fastest 9.1, 6.5, 6.2 and 6.0 m/s. **None of the six
  replays ends as its session did** (34 to 163 cm, in one with two hits only), as of the first session's two with blows and
  grabs: a recording of a person's interaction is a source of motion, not a regression test. Of ten recordings two replay
  (`vr_step4_2`, `vr_step4_4`).
- **The hand against its target** (take 6, 3 to 31 s, nothing touched; 4638 frames): 0.5 cm under 0.5 m/s, 1.3 up to 1, 1.9 up to 2, 3.8
  up to 3, 5.7 above, with the difference of two poses; 0.8, 3.3, 5.3, 7.5 and 12.4 with the runtime's velocity, 1.5 to 16.1 with
  none. Largest 21.4 cm.

**The owner's answers.** The hands are where the controllers are and do not tremble. The grab works well. The gesture of a new
take was meant each of the eight times. The punches are weak, and a fist hardly ever fells the body. A slap on the face should
turn the face the way of the slap. The skin of a raised shoulder was not noticed, so it is not far off. The get-up looks like
separate animations with pauses between them, and its end is very unnatural: the body comes onto its feet on almost straight legs
with nothing to push from, where a person gets up off the floor by bent knees; to be looked into and put right.

**Looked into the same day, nothing changed** (the physics asset was regenerated with another ankle for one trial and put back byte
for byte; the trials ran on `-Tune`).

*The pauses.* The clip's time waits for the body: in the four get-up scenarios 2.2, 2.5, 1.6 and 0.9 s of 5.4, 5.7, 4.8 and 4.1 s (back,
left, right, front), in the sessions by the same log lines 0.5 to 1.25 s at one clip time (0.73, 1.47, 1.88, 2.17 s; 4.4 to 5.8 s in all). The
first 0.5 to 0.75 s are the easing of the strength (`getup.ease_s`) with the clip at 0. The rest is the pelvis 16 to 35 cm from the clip's
place, mostly over the floor. With the pelvis's upright torque of the assist at 2000 N*m/rad and 500 N*m (600 and 200) the
waits are 0.6, 0.9, 0.6 and 0.1 s and the whole 3.8, 4.1, 3.8 and 3.3 s: what is waited for is a body that has pitched forward (its
pelvis is turned 22 to 39 degrees from the clip's). That is the world holding it up, which N2 is to take away, so it is a diagnosis
and not a cure.

*The last part.* Measured between a pelvis at 55 and 85 cm: the heels are 6 to 8 cm up on average (the clips have 0 to 4 from about 58 cm on),
the feet 26 to 33 cm from the pelvis over the floor (8 to 23 in the clips), and the trunk lies flat (over 70 degrees from upright) for 1.0 to
1.3 s in three of the four. At a pelvis of 78 to 80 cm the trunk is still at 59 to 72 degrees; clip R has it at 53 at 76 cm and at 31 at 84. So
the body stands on its toes with its hips high and then unfolds from horizontal to upright in half a second: the owner's "straight
legs". Two things of the clips themselves add to it: R and L rise hips first (their trunk is at 76 and 91 degrees with the pelvis
at 68 and 70 cm), while B squats (trunk 38 to 56 degrees, heels 0 to 4, pelvis 45 to 56 cm) and F half-kneels (26 to 36 degrees at 54 to 71 cm):
which is "by the knees". The body fell on its back in `getup_back` and got up by R.

| Tried (stall s, heels cm, trunk flat s, in back, left, right, front) | Result |
| --- | --- |
| `side_lag_cm` 35; `lag_cm` 25; `foot_lag_cm` 25; both lags | Less waiting (0.6 to 2.3 s), the same posture; two attempts given up with the side lag |
| `frame_follow_s` 0.1, 1.0; `pelvis_follow_s` 1.0; `rate` 0.7 | No better, or worse |
| The ankle's dorsiflexion 55 degrees instead of 20 (the clips ask for 21 to 73 of the 25 there are, by joint) | Front 4.1 to 3.7 s and lower heels; back, left and right as they were |
| Feet's assist 150 and 400 N (30) | Heels 3 to 5, two attempts given up, assist share 0.5 to 1.0 |
| Legs 1600 and 2400 N*m/rad (800) | The trunk at 80 cm gets flatter (79 to 95) |
| Spine 1500 N*m/rad (600) | Waits 1.5, 1.8, 1.6, 0.0 s; posture as it was |
| Everything together | 3.5 to 3.9 s with heels at 5 to 8 and the feet 23 to 26 away: the posture is the clips' way on this body, not a strength |

So no value of the tuning makes it right, and the ankle alone does not. What it needs is a way up of its own from the moment the
feet are under the body: the feet placed flat under the pelvis, and the rise by the knees, from a crouch, at a speed the centre of mass over
the feet allows. That is N7's two-stage get-up (step 5), with the clip carrying only the part on the floor.

*The slap.* A slap of the open hand at 6.2 m/s on the head (at 0, 4, 8 and 11 cm ahead of its centre) turns it by 1.1 to 2.6 degrees
about the vertical and tilts it sideways by 6 to 13. The hand's friction changes nothing to the digit (a body friction of 1.5 gives the same
run), nor does the flinch's giving way (`flinch_give_deg` 0). With a neck of 1 N*m/rad it is 12 to 13 degrees of turn and 24 to 26 of
tilt. So the head's neck muscle (40 N*m/rad) is most of it and the round shape of the head the rest: what the hand gives goes
through the axis. (Corrected on 26 November 2026: the world's hold on the head, `assist.head`, 10 N*m/rad over 0.2 s, took most of the turn; see "N8 A: the head as a body, and the wrist".) N8 A (the head on a joint of its own) is where it belongs; its "go or no-go" is to have a slap on the cheek turn
the head by 20 degrees or more, and the shape to have a face.

*The punch.* Jabs that stop at the surface, by speed (3, 4, 5.5 m/s): head 13, 27, 34 N*s, chest 33, 70, 86. The carry decides it:
`effectors.hand.strike_through_cm` 40 instead of 25 gives head 20, 41, 47 and chest 51, 101, 120, and then the fist at 5.5 m/s fells by
the head (knockdown) and by the chest (a fall), a fastest joint of 30 rad/s. Force 1500 N: 13, 28, 36 and 36, 79, 97. A hand of
3 kg: 14, 31, 35 and 36, 81, 88. `strike_s` 0.25: to the digit the same. The knockdown threshold at 25: 5.5 m/s fells, 4 does not. What the owner's hands
bring in the replays: head median 20 N*s at 2 to 3 m/s and 13 at 3 to 4.5, trunk 57 and 56: a person's fist comes at 2 to 4.5 m/s.

### The get-up: two attempts that failed, and what they showed (25 November)

**Note of 26 November 2026:** The get-up measured here is the clip-following one. Since 26 November 2026 the get-up is led (`getup.carry` 1, "R1: the led get-up"): 3.2 s, paused only in the 0.61 s ease, the plank 0.10 to 0.20 s while rising, the clip no longer laid over the loaded feet.

The owner asked for the get-up to be put right at once (a part of its own inside M4). Nothing of what was built is in the code:
the attempts were taken out again (`Saved\Recon\getup_kneel_attempt.patch`), all 112 scenarios have the hashes of `pre_getup`.

- **The owner's three frames** (`an_film_front`, 9.10 to 9.60): the body leans like a plank over its toes. From 8.3 to 9.3 s the feet
  carry 625 to 900 N, all of the weight, and the centre of mass is 25 to 44 cm outside them. The world holds it. Over the four get-ups
  it is more than 5 cm outside for 1.6 to 2.1 s, 45 to 65 cm at most. That is the criterion the get-up lacked: while the weight
  is on the feet, the centre of mass is over them.
- **First attempt:** the clip's own steps made as steps (a path for the foot, in relation to the body's pelvis). From the front better
  (feet 13 cm from the pelvis instead of 32, the trunk at 21 degrees instead of 49 at 80 cm), by the clips R and L worse, and a plank
  in the middle.
- **Second attempt:** a stand-up of its own from poses written as angles (both knees, a foot forward, the rise). It hung on its knees and
  fell over. The owner: much worse, nothing like getting up. I had called its first pictures convincing; they were not.
- **The recorded clips B and F by themselves look right on this body** (`an_ref_B`, `an_ref_F`: sit, half-kneel, up by the knees, feet flat).
  The source is good; following it is what fails.
- **The foot looked like the largest single cause** (four falls; the stand of 150 below does not bear it out). It is one rigid box of 26 cm, and the clips bend the toes. With a box of 18 cm (no toes)
  and an ankle that goes to 50 degrees, nothing else changed, the waits fall from 2.5, 1.6 and 0.9 s to 0.1, 0.6 and 0.6 (left, right, front),
  the trunk is never flat (1.3 and 1.0 s before from left and right), at a pelvis of 80 cm it is at 44, 45 and 38 degrees (72, 65, 49), the heels are
  2.5 to 3.6 cm up (7.2 to 8.0) and the centre of mass is off the feet for 0.7 to 0.9 s (1.6 to 2.1). The ankle alone did not do it. From the
  back the attempt is given up. The feet still stay 22 to 28 cm behind the pelvis, so it is not the only one. Trial only: the asset was put back
  byte for byte.
- **The pauses** go when the clip does not wait and the pelvis is followed harder (400 N and 500 N*m, 90 and 200): 3.2 s, as recorded, at an
  assist share of 0.4 to 0.5. That is the world's help again.

### N8 D brought forward: a foot with toes, first step (25 November)

The owner's decision: the toes first, then the get-up again, with a film shown at every step. A second physics asset beside
today's (`Specs/physics/npc_body_toes.json`, `PA_SandboxNPCToes`: 20 bodies, the foot's box cut at the ball, a toe of 0.14 kg on a hinge of 30
degrees down and 60 up, a muscle group of its own, `muscles.toes`). The sole, the load and the surface of a foot take its toes in (any body
that hangs on the foot's). Today's body: 112 scenarios with the hashes of `pre_getup`, 42 unit tests. No scenario uses the new asset.

- **Standing:** pelvis drift 0.90 cm (0.91), largest settled turning speed 0.20 rad/s (0.19), assist share 0.0071 (0.0063, an eighth more; a fifth is
  allowed). The physics step alone on the machine, three runs each: 0.60 ms against 0.56, so 0.04 of the 0.05 that is allowed.
- **Pushes: the go/no-go is not met.** Push matrix 157 of 160 (today 160): `neck_01` 270 at 30 N*s two steps, `spine_04` 45 at 80 and `pelvis` 90 at 80 a fall;
  and runs of ten, nine and five steps in cells that pass. Of seven scenarios tried, `push_chest_80_steps`,
  `push_pelvis_80_side_steps` and `hit_chest_200_falls` fail. Nothing has been tuned for it yet.
- **The get-up on 150 falls** (the stand, seeds 1 to 3, the same tool for both bodies): 149 end standing on both, at the first attempt 132 today and 126 with toes.
  Attempt that succeeds: 4.97 and 4.72 s. Centre of mass more than 5 cm off the feet while they carry over 0.8 of the weight:
  1.45 and 1.24 s on average, 50 and 48 cm at worst on average. By clip: B 1.86 and 1.89 s, F 1.24 and 0.91, L 1.63 and 1.24, R 1.29 and 1.31. Steps made
  after standing up, in the rest of the run: 1.15 and 1.36. The toes help F and L somewhat and B and R not at all; the plank is still there.
- **The four falls shown first said more than that, and wrongly.** In three of the four (back, left, right) the body with toes fell otherwise and got up by
  another clip (R and B, L and F, R and F), so those rows compare clips and not feet. With a toe muscle of 30, 100 and 300 N*m/rad the wait in one fall goes
  1.1, 0.3, 1.8 s: noise. Four falls are not a sample. The same doubt lies on the short-foot trial above (four falls, the clips not checked, the ankle's range widened with it).
- **What the clips ask of the foot, key by key** (the first version of this line gave the largest excess on any axis, 18 to 56 degrees, as if
  it were one thing: the 56 is sideways, swing2, range 10). The toes hardly bend in the clips: 21 degrees at most. The ankle, where the weight comes onto a foot
  (1.2 to 1.8 s): shin forward over the foot to 22 to 49 degrees, the range gives 30. On the knee, and pushing off: toes down to 64, the range gives 20; sideways
  to 50 and twisted to 27 (49 at the start of B) while lying and kneeling.
- **The ankle's range, measured on the 150 falls** (decided before the runs: if the mean stays above 0.7 s, the toes are put off). Wide
  is swing1 -55 to 45, swing2 -15 to 35, twist +-25. Mean time off the feet, s; by seed; ended standing; first attempt; push matrix:

  | Body | off, s | by seed | stood | first | matrix |
  | --- | --- | --- | --- | --- | --- |
  | today | 1.45 | 1.45 1.46 1.44 | 149 | 132 | 160 |
  | toes | 1.24 | 1.18 1.19 1.34 | 149 | 126 | 157 |
  | W0 ankle wide, no toes | 1.14 | 1.10 1.10 1.23 | 150 | 128 | 158 |
  | W1 toes and ankle wide | 1.06 | 1.11 1.09 0.99 | 148 | 125 | 159 |
  | W2 toes and the shin to 45 only | 1.20 | 1.16 1.21 1.24 | 149 | 127 | not run |

  By clip W1 is 0.67 s in F, 0.82 in L, 1.38 in R and 2.11 in B (15 falls); W0 0.83, 1.07, 1.41 and 1.47. Most of what there is
  to gain is the ankle's; the toes add a little in F and L. None of them comes to 0.7, and worst cm is 41 to 44 against 50 today. **The toes are put off
  to N8 D as the order had them.** W0's matrix misses are two blows of 200 N*s on the chest from 45 and 135 degrees that the body steps through
  where it fell.
  Film, one fall a clip, the first on seed 1 that all three bodies got up from at the first attempt by the same clip:
  `Saved\Films\getup_same_clip.png`. The rows look alike.
- Today's asset was made again by the generator with the new one: its bytes differ (md5 024a087b, was b799ab98), its runs do not (112 hashes).
  Film: `Saved\Films\getup_now_vs_toes.png`.

### Why the body leans: the clip against the body, frame by frame (25 November)

**Note of 26 November 2026:** The get-up measured here is the clip-following one. Since 26 November 2026 the get-up is led (`getup.carry` 1, "R1: the led get-up"): 3.2 s, paused only in the 0.61 s ease, the plank 0.10 to 0.20 s while rising, the clip no longer laid over the loaded feet.

Measuring only, on the stand (a temporary log of the clip's and the body's poses in one frame; taken out again; the stand with the log had the hashes of the
stand without it, 150 of 150; the suite afterwards 112 of 112). The first table has 149 get-ups; the second, of turns, the 99 of seeds 2 and 3
(seed 1 gave no result: processes of `getup-seeds.ps1` that clean old queue folders at the same time collide, `common.ps1:248`; not repaired).

- **The clip stands; the body does not follow it.** Distance of the centre of mass from the line between the feet, cm, clip and body, at a clip time of 1.0, 1.4, 1.8 s:
  F 55/65, 46/50, 13/40; L 57/58, 46/51, 15/40; R 30/49, 22/44, 4/28; B 33/35, 26/37, 8/33. From 1.4 (R from 1.0) the clip brings it in, the body does not.
- **The body is pitched forward as one piece.** Off the clip at clip time 1.6 to 2.2 s, degrees: feet 12 to 37 (heels 13 to 14 cm up, the clip's 5 to 6), shins 16 to 34, thighs 9 to 28,
  pelvis 5 to 26, chest up to 37; the joints between them are mostly within 0 to 13 (the knee to 17, F's ankle 20 at its range's end). So the pose is nearly the clip's and the turn is of the whole.
- **Where it begins: the moment the hands leave the floor** (last time the lower hand is under 9 cm; medians). Centre of mass off the feet, body and clip, cm: B 35 and 27, F 47 and 27,
  L 44 and 25, R 43 and 22. In none of 149 falls is the body within 10 cm of its feet then. The clip is not either. Its closing speed at that moment,
  cm/s, clip and body: B 47 and 19, F 104 and 31, L 84 and 10, R 25 and -20. (Corrected: the first version of this line called it the speed of the centre of
  mass and said the clip "falls back onto its feet". The figure is how fast the gap closes, feet included, and the next part shows that the feet do it.)
  The hands leave the floor at the clip's time all the same, and the body then holds the lean for 1.7 to 2.1 s, by the world's help.
- **What it is not.** Not the pauses alone: in F the clip waits 10% of the second before and the body is as far off; in L 69%, in R 49%, in B 37%. Not the toes
  (above), and the ankle's range is a part only (W0: 1.14 s of 1.45). Within a clip the distance at liftoff and the time off the feet afterwards go together only loosely (0.61, 0.34, 0.46 and -0.23
  in B, F, L, R), so the liftoff is where it shows and may not be all of the cause. What makes the body slow there (arms' push, legs, the clip's waiting)
  is not measured.

### What keeps the gap open: the feet are loaded when the clip moves them (25 November)

**Note of 26 November 2026:** The get-up measured here is the clip-following one. Since 26 November 2026 the get-up is led (`getup.carry` 1, "R1: the led get-up"): 3.2 s, paused only in the 0.61 s ease, the plank 0.10 to 0.20 s while rising, the clip no longer laid over the loaded feet.

The same stand, 149 get-ups, hashes as without the log (150 of 150; the log's code is out again, suite 112 of 112). The floor's push on the bodies reads 848 N over a
get-up against a weight of 784 (8% too much; 749 at its end), so shares of weight below are good to about 10%. Horizontal forces of contacts could not be
checked and are not used.

- **The clip closes the gap with its feet.** Between clip 1.3 and 1.9 s its feet go forward by 18, 31, 55 and 35 cm (B, F, L, R) and its centre of mass moves by 11 cm at most. The body's
  feet go 2, 7, 9 and -9 cm. Its centre of mass comes back instead, 13, 3, 25 and 60, and later (1.9 to 2.3) 17 to 40 while the feet slide back by up to 19.
- **A foot that carries weight does not go.** Each move of a clip foot (40 cm/s and more): a body foot that carries under 40 N goes 41 of the clip's 45 cm in F, 52 of 59 in R, 14 of 31 in L; one that
  carries 40 N and more goes 35 of 52 in F, 20 of 48 in R, 13 of 85 in L, 8 of 54 in B. The body's foot is loaded (40 N and more) in all the clip's foot moves of B, in 67% of F and R, in 33% of L.
- **The arms carry too little of the weight, though the clip needs them to.** In the half second before the liftoff the arms should carry 0.60, 0.49, 0.42 and 0.42 of the weight for the body to balance on
  its feet and hands (B, F, L, R); they carry 0.07, 0.22, 0.29 and 0.27. Shoulders are at their cap in 5, 24, 42 and 40% of those frames, elbows in 0 to 29%, knees in 0 to 1%, hips in 36, 1, 32 and 11%.
  After the liftoff hardly any muscle is at its cap. The pelvis's assist torque is at its cap of 200 N*m, where 166 to 254 N*m would hold the lean.
- **Two interventions, by `-Tune`, files unchanged, and both failed.** Pelvis torque cap 100: 115 of 150 end standing, 25 at the first attempt, and in those that stand in F the time off the feet is the same (1.24 s, as with 200).
  (0 there means no cap, so the run with 0 tested nothing: 1.39 s.) Arm cap 300 instead of 120: 147 of 150, 105 at the first attempt, 1.51 s. The body does not lean
  because a force is too small. The assist torque is what holds it up, not what keeps the arms from being loaded.
- **What is measured and what is not.** That a loaded foot does not move with the clip is measured. That the loaded feet are the cause of the lean is the reading of it, with the free foot (90% of the
  clip's travel in F and R) as the comparison and L (45%) against it. The two processes of `getup-seeds.ps1` that clean old queues still collide now and then.

### The get-up, third attempt: the foot held until it is free (25 November). Put off

**Note of 26 November 2026:** The get-up measured here is the clip-following one. Since 26 November 2026 the get-up is led (`getup.carry` 1, "R1: the led get-up"): 3.2 s, paused only in the 0.61 s ease, the plank 0.10 to 0.20 s while rising, the clip no longer laid over the loaded feet.

The limit was set before the runs: time off the feet 0.7 s at most, 146 of 150 end standing, 125 at the first attempt. **Not met, taken out again**
(`Saved\Recon\getup_step_hold.patch`; suite 112 of 112 with the hashes of `pre_getup`, 42 unit tests). The get-up waits with its cause written down.

- **What it was:** a foot that the clip is about to move and that carries 40 N or more is lifted by the leg's own placement, the clip waits for it (1 s at most), the pelvis
  is aimed toward the other foot and the hands on the floor, and once free the foot goes with the clip's.
- **The first version did not test the idea:** 96 of 122 waits ended after 0.06 s, by a contact that flickered off, and the leg was let go at once. It gave 1.42 to 1.45 s (1.45 today).
  Put right once: free for 0.1 s without a break, kept lifted to the end of the clip's move. Hands on the floor were also read after their reading had been set back; fixed with it.
- **The second version,** three settings, 150 falls each (all three, none chosen; lift 6 or 12 cm, shift 10 or 0). Time off the feet by clip in the last four columns:

  | Run | off, s | stood | first | B | F | L | R |
  | --- | --- | --- | --- | --- | --- | --- | --- |
  | today | 1.45 | 149 | 132 | 1.86 | 1.24 | 1.63 | 1.29 |
  | T1: lift 6, shift 10 | 1.05 | 147 | 99 | 1.05 | 0.70 | 1.29 | 1.45 |
  | T2: lift 6, no shift | 1.12 | 145 | 99 | 1.22 | 0.73 | 1.42 | 1.45 |
  | T3: lift 12, shift 10 | 1.09 | 147 | 86 | 0.89 | 0.85 | 1.53 | 1.39 |

  On the falls that both bodies got up from at the first attempt by the same clip (T1): B 6 falls, 1.85 and 1.04 s; F 38, 1.24 and 0.72; L 34, 1.63 and 1.25; R 19, 1.39 and 1.41.
- **What it shows:** the loaded foot is a cause in F and B (the lean is shorter by 0.5 and 0.8 s on paired falls), a smaller one in L, and not the cause in R. One fall in four then needs
  another attempt, so the foot that is held spoils the rest of the get-up. Nobody has looked at why; R was not explained.
- Film: `Saved\Films\getup_step_hold.png`, one fall on the bare floor a clip.

### The punch, how far a fist is carried: what each value does (25 November, for the owner's decision)

`effectors.hand.strike_through_cm`, by `-Tune`, nothing changed. A fist at a target that stops at the surface (`vr_punch_sweep`), N*s delivered at 3, 4 and 5.5 m/s, head then chest, and the suite of 112:

| cm | head | chest | suite |
| --- | --- | --- | --- |
| 25 (now) | 13 27 34 | 33 70 86 | 112 pass |
| 28 | 14 30 38 | 37 77 94 | 112 pass |
| 30 | 15 32 40 | 39 82 99 | 112 pass |
| 32 | 16 34 42 | 42 86 103 | 111, `palm_open_close` fails |
| 35 | 18 36 45 | 45 92 110 | 112 pass |
| 40 | 20 41 47 | 51 101 120 | 106, six fail |

At 25 to 35 no fist of 3 to 5.5 m/s fells the body; at 40 the 5.5 m/s one does, into the head and into the chest. At 40 the scripted punch of 8 m/s on the head, the chest and the belly fells it too, where
five scenarios said "stands": `effector_hit_head`, `strike_punch_head`, `strike_punch_torso`, `strike_punch_belly_steps`, `palm_open_close`. The sixth, `strike_punch_shoulder`, was read wrong at first (as a lower impulse): it
counted two hits where one is expected. 32 is an edge of one scenario (35 passes). The fastest joint of the strike in `strike_punch_torso` is 32.5 rad/s, a foot of the falling body (33.5 in the knockdowns).

**The owner's decision, 25 November: 40.** Built: `effectors.hand.strike_through_cm` 40, and the new `strike_return_cm` (15).
- **The return of a fist is no new blow.** When the blow ends (0.15 s) the fist, 40 cm beyond where it landed, springs back to a hand that has gone back, at 6 to 8 m/s, and met the shoulder from behind: `strikes` 2 and a second flinch. The same return was there at
  25 cm (77 N on the shoulder). Until the fist is within `strike_return_cm` of its target no blow begins (`strikes` 1). With 25 and 0 the suite is `pre_getup` to the digit (112 of 112). The hand still brushes the shoulder as it comes back (5.2 N*s), so that scenario expects two hits: the body is told of a hit, the blow is one.
- **What was changed in the scenarios:** the five above expect a fall (`end_state` lying), `strike_punch_head` a knockdown, `strike_punch_torso` a joint speed of 40 at most; `strike_punch_shoulder` two hits. Eleven scenarios have other hashes, all of them with a fist.
- **Measured again:** suite 112 of 112, push matrix 160 of 160 (every cell as `pre_getup`), knockdowns 8 of 8 by a blow and by two hands, grab pulls 93 of 93, stand of 150 falls 149 (as before), record and replay 12 of 12, 42 unit tests. Baseline `through40`.
- **Impulses, N*s at 3, 4, 5.5 m/s:** head 20, 41, 47; chest 51, 101, 120; belly 60, 127, 154.
- **Seen on the film** (`Saved\Films\punch_40.png`): in `effector_hit_head` the body lies on its back with its hands 51 cm up at 4.5 s and 30 cm up from 5.5 s on, where the other lying bodies hold theirs at 8 to 24. A matter of the fall's pose and of N7, not of the punch.
- The recorded sessions were made with 25; their replays are not comparable.

### M4 part 3, what is left of it: the wrist and a lifted body, facts (25 November)

**Note of 26 November 2026:** Way (b) was tried in the night of 25 to 26 November (`PA_SandboxNPCWrist`, the swings limited in the hand's frame): 113 first attempts of 150 against 132; not taken ("N8 A: the head as a body, and the wrist").

No code, no tuning and no asset was changed.

- **The wrist.** A joint's angles are measured in the forearm's frame (twist 70, swing1 60, swing2 25 each way). The four get-up clips ask the hand for a swing2 of 32 to 98 degrees at a twist of 11 to 60 (`hand_r`: 60 to 73
  beyond the range; `hand_l` 7 to 63; B of `hand_l` is the small one), which is the hand bent back with the forearm turned. The bend of 85 to 98 degrees is also more than the 60 that the range of swing1 gives, so a wrist whose limits
  count in the hand's own frame and reach 85 degrees would serve most of it (the largest ask is 98). Where it has been tried (a swing2 of 85, M3 part 7) it helped the get-up little. The other user, a hand on the
  floor in a fall (N6), is not built yet.
- **Two ways to correct it, neither tried:** (a) the forearm's turn taken to the elbow, as in a person (a `lowerarm` that twists 70 degrees, a wrist that does not): the clips and the poses have to hand the hand's turn over, the skin twists at the elbow and has to be looked at. (b) The joint's two
  frames exchanged, so that the solver counts in the hand's frame: our code takes the child as the first bone of a joint, and the projection of the joint would move the forearm instead of the hand.
  Either changes the physics asset and with it every hash.
- **A lifted body.** A lying body taken by the chest with two hands and drawn up 1 m: the hands are 300 N each, 600 of 785 N of weight; the chest rises from 16 to 34 cm, the hands are then 45 cm away, the grip slips at 10.5 s and the body sinks back. With one hand it rises from 17 to 22 cm. The body
  stays in Lying and limp all along and the head follows the trunk. No hand can lift it off the floor (`Saved\Films\lift_two`), so the state of its own that the plan asks for has no situation to be in.

### M4 part 4: what a stick's blow is made of, the sweep again (25 November, nothing changed)

**Note of 26 November 2026:** Superseded the same day by "M4 part 4: the blow of a thing in the hand" (`strike_held` 1, `strike_hold_scale` 5).

The 12 scenarios of the stick sweep (95, 120, 140 and 162 cm at 3, 5 and 7 m/s), by `-Tune`: the impulse the body's contacts add up to, N*s.

| | 95 cm: 3, 5, 7 m/s | 120 cm | 140 cm | 162 cm | Hits told in 12 |
| --- | --- | --- | --- | --- | --- |
| Now | 14, 30, 36 | 14, 14, 51 | 13, 12, 3 | 2, 2, 0 | 2 (5 to 6 N*s each) |
| Wrist and hold 5 times stronger (100 N*m each, 750 N*m/rad) | 48, 47, 45 | 55, 48, 48 | 56, 48, 7 | 5, 4, 4 | 8 |
| Hand 900 N, 9000 N/m, wrist as it is | 14, 14, 13 | 14, 13, 13 | 13, 12, 12 | 2, 2, 0 | 0 |
| Both | 52, 48, 48 | 57, 50, 49 | 56, 46, 7 | 5, 4, 4 | 8 |

- The wrist and the hold decide, not the hand's strength: three times the hand's force and spring change nothing.
- **It does not grow with the speed in any of them.** With the wrist strong it is flat at 45 to 57 N*s whatever the speed, and the body tells a hit of 5 N*s: what is delivered is the hold pressing on, as with a punch before the blow. The plan's "the impulse grows with the speed" is
  met by the fist because its blow is graded by the speed it arrives at (`strike_speed_mps`), and a thing in the hand has no such blow.
- At 162 cm (head) the stick passes without touching in all; at 140 and 7 m/s it nearly does. A wrist of 100 N*m is four times a man's (25 N*m).

### M4 part 4: the blow of a thing in the hand (25 November, the owner's option A)

`effectors.hand.strike_held`. A thing in the hand that meets a simulated body at `strike_speed_mps` or faster (the speed of the point that met it, read before the step) has the hand's blow for `strike_s`: cap up to 900 N, spring up to 9000 N/m, 40 cm
through, and the thing is held and turned up to `strike_hold_scale` times as firmly; the strength grows with the speed up to `strike_held_full_speed_mps` (9). The hand's slip and tear rules do not count in a blow. A fist's own blow is unchanged.

Off, the 112 scenarios of before have the hashes of `through40`. On for the whole suite, 113 pass and one has another hash (`prop_stick_strike_arm`, which is a stick).

The limit set before the runs: the felt impulse grows from 3 to 5 to 7 m/s in three places of the body, nothing goes 2 cm into it, scenarios with a fist and without things keep their hashes. **The first test failed, and what it was is said here.** The 12 scenarios of the old sweep
(`stick_sweep`, hand at x = 88): grows from 3 to 5, falls at 7 in two places of three. Read by a temporary log of the contacts: at 7 m/s the stick meets the trunk for 4 to 14 frames against 69, because 17 to 20 cm of it overlap the body and the tip lags the hand. Moved 18 cm nearer (x = 70, the same swing) it lies on the trunk in all. That is a change of the test after its result, and it is the test that
was wrong: the figures for x = 88 are left above. And the blow's own speed limit (4.5 m/s, a fist's) flattened 5 to 7, so things got one of their own (9).

Contact impulse, N*s, x = 70, by point and speed (3, 5, 7 m/s):

| | 95 cm | 120 cm | 140 cm | 162 cm (head) |
| --- | --- | --- | --- | --- |
| Off | 19, 18, 19 | 19, 18, 17 | 18, 19, 18 | 23, 15, 17 |
| Hold x1, full speed 4.5 | 20, 20, 18 | 19, 17, 16 | 19, 21, 20 | 18, 15, 31 |
| Hold x5, full speed 9 | 22, 26, 29 | 21, 25, 30 | 20, 24, 29 | 27, 25, 6 |
| **Hold x10, full speed 9** | 25, 35, 42 | 24, 33, 45 | 24, 33, 40 | 30, 30, 20 |
| Hold x15, full speed 9 | 27, 48, 54 | 26, 41, 56 | 25, 39, 50 | 25, 34, 29 |

In the chosen row it goes 0.23 cm into the body at most, no joint faster than 16.7 rad/s, no grip slips or tears, the body stands and one hit of 5 to 6 N*s is told at 5 and 7 m/s (none at 3). **The head (162 cm) does not meet the criterion:** the stick lies there for a moment only. Nothing above 45 N*s at 7 m/s,
where a fist at 5.5 m/s is 120 into the chest: the stick is 0.8 kg held at one end, and what is delivered is the hold. `strike_hold_scale` 10 is 400 N*m at full strength, not a man's wrist; it lasts 0.15 s. On the film
(`Saved\Films\blow_torso`) the stick comes to the trunk and lies against it for the rest of the sixth of a second; the body hardly moves. Scenario: `prop_stick_blow_torso` (120 cm, 7 m/s).

**The owner's decision, 25 November: on, with a hold of 5 times** (21, 25 and 30 N*s at 120 cm, half the wrist's torque of x10); the hold is tuned after the owner's ten minutes with a stick in a session. With it: suite 113 of 113 (one other hash, `prop_stick_strike_arm`, the stick), push matrix 160 of 160, grab pulls 93 of 93, session grab demo and record and replay pass. Baseline `held5`.

### M4 part 4: furniture with mass in a room (25 November)

**Note of 26 November 2026:** `reactions.thing_support` is on since later that day: the body relaxes over the table's edge ("M4 part 4: support from things…").

A thing of a scenario can be made of boxes (`"boxes"`): one rigid body with the boxes as its shapes (`USandboxPropComponent`), so a table is its top and four legs. Welded components were tried first and weighed 30.9 kg and 9.7 kg where 30 and 5 were
asked, because a welded part adds a mass of its own; with one body they are 30 and 5. A room's `furniture` piece may have `mass_kg`: it is then no part of the map but a thing the scenario or the session that loads the map puts there. `movable_room.json` is `getup_room` with a table of 30 kg
and a chair of 5 kg; its map is baked (Preview). Everything that was there before has the hashes of `held5` (113 of 113).

**The stand of falls, static against movable, three seeds** (`Tools\getup-stand.ps1 -FurnitureRoom movable_room`, the same falls; the switch `thing_support` off):

| | Table and chair only: end standing | at the first attempt | All 50: end standing | at the first attempt |
| --- | --- | --- | --- | --- |
| Static (`getup_room`), 3 seeds | 15, 16, 16 of 16 | 7, 8, 11 | 50, 50, 49 | 43, 45, 44 (`through40`) |
| Movable (`movable_room`) | 16, 16, 16 of 16 | 13, 13, 15 | 50, 49, 50 | 46, 45, 43 |

A falling body shoves the furniture aside: of the 150 the chair went 2.9 m at most and moved at 4 m/s at most; the table tips and slides. A chair and table are in no one's way for long: at the table and the chair it gets up at the first attempt 41 times of 48 against 26. Nothing was launched. The deepest anything goes into the world is 2.5 cm (a wall). The plan's criterion (repeated with movable furniture on three seeds) is met for this furniture.
The static figures of the whole stand are the present ones of `getup_room`, not measured with this tool's new parameter.

**Seen on the film** (`Saved\Films\furniture_movable.png`, falls 35 and 42 of seed 1): a body that falls over the table's edge keeps its legs straight out in the air for a second (3.6 to 5.0 s), like a plank, because the muscles let go when the trunk meets the *floor* and a table is not
the floor to it: what `reactions.thing_support` decides (off). And a body that stands against the tipped table bends forward over it (5.5 to 7 s) before it goes down.

Scenarios: `room_movable_rest` (both stay where they are) and `room_movable_fall_gets_up` (fall 42: falls against the furniture and gets up). One unit test. The tool's `-FurnitureRoom` is named so because PowerShell has no case: a `-Room` would have been overwritten by the script's own `$room`.

### M4 part 4: support from things, the rule made physical (25 November), on by the owner's decision

`reactions.thing_support` was the rule "what pushes up is support unless it pushes hard" (5 N*s a substep for parts other than the feet). It now is: **a simulated thing that has a weight of its own and does not come up faster than `thing_support_max_up_mps` (1 m/s) where it touches** supports what it pushes up on, however hard
it pushes. A hand has no weight (its gravity is off), so a hand is never support; a fall onto a table is a hard push and is support; a stick swung up is not. `thing_support_max_ns` is gone.

Limits set before the change, and how it did (the switch on for the whole suite of 115):

| Limit | Result |
| --- | --- |
| Scenarios without things have the hashes of the switch off | Met: only five with things differ (`prop_fall_on_box`, the three `prop_*_on_plate`, `room_movable_fall_gets_up`); with the old rule `getup_held` and `grab_getup_held` differed too, and `grab_getup_held` failed |
| Hits told in falls onto things: at most 4 in `room_movable_fall_gets_up` and `prop_getup_on_plate` | `prop_getup_on_plate` 4 (11 off). **`room_movable_fall_gets_up` 7 (12 off): not met**, of which one is the blow, two are the furniture's sides as the body falls and four are the table tipping onto the body as it lies: hits from the side and from above, which the owner's rule makes hits. The limit was too low |
| A body that falls over the table's edge: feet no higher than the pelvis + 10 cm within 1 s of the trunk touching the table | **Not met by 0.1 s: 1.1 s** (feet 119, pelvis 112 cm at 4.4 s, touch at 3.28). Off it is never, up to the fall off the table (the feet 10 to 50 cm over the pelvis until 5.5 s). The muscles let go 0.5 s after the touch, as on the floor |
| Stand with movable furniture, three seeds: 147 of 150, 125 at the first attempt | Met: 149 of 150 (50, 50, 49), 130 at the first attempt (134 off) |
| 115 scenarios pass | 114: `prop_fall_on_box` expects the body to feel 100 N*s of the box; it is support now and felt 0 (its expectation would be dropped) |

Hits a fall over the 150 falls: 2.83 off, 2.51 on; while falling, lying or getting up 1.79 and 1.43. Film: `Saved\Films\thing_support.png` (fall 35). On, the body relaxes over the table's edge and slides down; off it lies stiff on it with its legs in the air.
Off, all but the scenarios that switch it on for themselves have the hashes of `furn` (one other: a `prop_*_on_plate` scenario, whose rule changed).

**The owner's decision, 25 November: on.** `prop_fall_on_box` lost the expectation that the body feels the box (it lies on it now). With it on: suite 115 of 115 (five scenarios with things have other hashes), push matrix 160 of 160, knockdowns 8 of 8 by a blow and by two hands, grab pulls 93 of 93, stand of 150 falls 149 (132 at the first attempt, as off: no things in it), session grab demo, record and replay. Baseline `support`. No limit on hits is set in its place.

Afterwards: a plate that slides and a thing that tips under the feet (the next section).

### M4 part 4: a thing that moves under the body, sticks underfoot, sticks fast (25 November)

**A plate that slides.** The body took its support as still: the capture point was worked out from its velocity over the world and its anchors stood in the world. `CarrySupport`: the velocity of what carries the feet (by the contacts, weighted by how hard each foot was pushed up; the floor is still, below 2 cm/s it is 0) is taken off the velocity the
balance sense uses, and the anchors, foot placements, the step under way and the remembered support go with it. Over a floor that is still it is exactly 0: the hashes of `support` (114 of 115; the other, `prop_step_on_plate`, is on a plate that moves a little). A thing is driven in a scenario by the new event `prop_move` (a strong spring to a point that moves: a velocity set from outside
is undone by the floor's friction within the step).

A plate of 400 x 400 x 10 cm and 80 kg driven from rest, 8 s, `Saved\Recon\plate`:

| Plate, m/s (ramp 1 s, "jerk" 0.1 s) | Before: recovery steps, end | After |
| --- | --- | --- |
| 0.13 | 2, stands | 0, stands; the pelvis goes 96 of 97 cm with the plate |
| 0.30 | 12, stands | 0, 223 of 225 cm |
| 0.30, jerk | 8, still in a stagger | 0, 236 of 238 cm |
| 0.60 | 17, still in a stagger | 0 (4 foot lifts counted from outside), stands |
| 1.00 | 2, falls | 4, ends in a stagger |

Limit set before: no step at 0.13 and 0.3 m/s, the pelvis within 10 cm of where it would be on the plate: met (2 cm), also with the jerk. 0.6 m/s stands without a recovery step; 1.0 m/s staggers and stays up, where it fell before. `prop_ride_plate` (0.3 m/s).

**Sticks underfoot** (4 x 4 cm, 0.8 kg; the body put on them at its feet): under both feet across (60 cm) it stands, no step, no hit (`prop_stand_on_stick`); under one foot only it steps twice as it comes down 4 cm and stands, no hit. A plank on a roll, 14 or 30 cm off its middle: it stands on it as it tips 4 to 5 degrees. Not run: a chair leant on (the falls among movable furniture lean on it).

**The stick to 12 m/s** (hold x5, the swing of x = 70 made faster): the hand reaches 12 m/s at 9 and 11 m/s asked, the stick's middle 9.5 to 11.5, and nothing goes deeper than 0.8 cm. The impulse grows up to 9 m/s (120 cm: 21, 25, 30, 62 N*s at 3, 5, 7, 9; 140 cm: 20, 24, 29, 37; 95 cm: 22, 26, 29, 33) and at 11 falls in two
of three places (10 and 8 N*s, and 68 at 95 cm): the hand at its 300 N does not follow and the stick grazes. The head (162 cm) is not reached by the sweep.

### M4 part 3, its two open questions: a hold that is felt, and the standing leg (25 November), on by the owner's decision

**Note of 26 November 2026:** Since "R1: the led get-up" (26 November) one hand no longer slows a get-up (42 of 50 pull free, seed 1); `grab_getup_held` fails on purpose until R2 repairs the hold by one hand.

**A cap on the pelvis's assist is not the answer to the leg** (`assist.pelvis.max_force_n` 80, the first time with the whole set): 117 scenarios but one pass (`grab_pull_standing` falls), matrix 160 of 160, knockdowns 8 of 8, stand 149 of 150, 93 pulls; of the 24 pulls on thigh and calf 2 bodies come along (2 that are stepped without it: calf up), and the arm pulls get worse
(44 stood instead of 51). What was written that "at no cost" held for the push matrix alone.

**`reactions.hold_felt`**: what the hands that hold the body pull with (told by the run and the session, `TellHolds`, now with its direction: along the hand's spring, as strong as the joint carries) counts as a push that is kept up, as contacts do (`held_push_s`): the body leans into it, steps for it, and while it gets up it is pressed by it. Off, the 117 scenarios have the hashes of `part4`.

Limits set before the runs, with it on:

| Limit | Result |
| --- | --- |
| Off: every hash; on: only scenarios with a hold differ | Met: `grab_getup_held`, `grab_lift_arm`, `grab_palm_lift_arm`, `grab_pull_standing`, `grab_two_hands_one_arm`; the suite passes |
| Thigh or calf pulled: the body comes along in at least 6 of 24 (2 before) | Met: 9 of 24, four with the grip still holding |
| `grab_pull_standing` passes, and not more falls in the 93 pulls than now (1) | Passes; **falls 2, not met** (the chest pulled left at 2 m/s with 265 N falls; before it stepped and stood) |
| One hand on a get-up: 20 to 60 of 150 pull free; two: at most 5 | Met: 46 (47 off); none (none off) |
| A stand without holds as it is | Met by the hashes |

**The owner's decision, 25 November: on.** The drop with two hands (139 of 150 against 144) was seeds 1 to 3's: on six seeds, 300 falls, 286 off and 284 on (new seeds 4 to 6 142 and 145), none pulled free but one off. With it on: suite 117 of 117 (five `grab_*` with other hashes, no expectation changed), matrix 160 of 160, knockdowns 8 of 8, 93 pulls, session grab demo, record and replay, stand 149 of 150 (132 first). Baseline `hold`. Outcomes of the 93 pulls: stepped 57, stood 34, fell 2 (off 41, 51, 1): the body gives way to a pull more readily. Film `Saved\\Films\\hold_felt.png`: on, a thigh pulled to the side or forward is followed by a step of that leg, and the arms go out; off nothing happens and the hand slips off.

### The natural body, reconnaissance before N8 and N2, 2026-11-25

**Note of 26 November 2026:** Superseded in part on 26 November 2026: cases a and b are repaired (`lift_smooth` 1, `release_smooth` 1, `land_ease_s` 0.05, `fall_ease_s` 0.1; "N2: four of the jumps' repairs on"); C25's baseline is 439 N*m (docs/natural-body.md, decision 15).

Two questions of `docs/natural-body.md` answered without changing how the body behaves: what close-ups show of the
hands and the skin (step 1 of N8), and where the jumps of C25 come from. **Nothing moved:** against the baseline
`pre_recon` (which is `pre_n1` to the digit, and N1's ten `measure_*`) all 111 scenarios have the same two hashes; one
scenario is new. The suite takes 43 s; 42 unit tests, one of them new. The corrections that were tried to prove a cause
were behind a variable and are out again (`Saved\Recon\trial_corrections.patch`; the verdicts of its suites and matrices beside it).

**Close-ups** (`Saved\Films\n8_*`; the sheets are `n8_hand_stance.png`, `n8_hand_fall.png`, `n8_twist_0.png`,
`n8_twist_p70.png`, `n8_twist_m70.png`, `n8_raise_side.png`, `n8_raise_side_clav.png`, `n8_raise_front.png`,
`n8_knee120.png`, `n8_knee120_hip.png`, `n8_correctives_with_without.png`, `n8_grab_lift.png`). In the test room, the pelvis
welded, the idle off, the arms at the get-up's 1200 N*m/rad and 120 N*m so that the pose is reached; the fall is the
body's own.

| Pose asked for | Angle that came out | With the body's own arms (40 N*m/rad) | What is seen |
| --- | --- | --- | --- |
| Wrist twisted +70 and -70 | 69.9 and -69.9, at the end of its range (70), not beyond | the same | The skin holds: no crease, no "candy wrapper". The turn is spread along the forearm; the wrist is a little narrower than untwisted, as a person's is |
| Arm raised 150 sideways (swing1 116) | 151.0 from the vertical | 141.6: the weight takes 9 degrees | The shoulder does not rise with the arm: no collarbone is asked for. The armpit and the top of the shoulder hold |
| The same with the collarbone (30) and the arm (90; 85 in the pictures) | 155.5 (90) | 147.1 | Nearer a person's shoulder; skin holds |
| Arm raised 150 forward (swing2 150) | 145.3 from the vertical, 147.6 from the chest: **at the joint's end**, 0.9 degrees beyond the 150 its range has from the reference pose | 139.7 | A deep notch behind the shoulder: the arm goes up and the shoulder blade does not |
| Knee bent 120, thigh down | 120.4, pelvis raised 45 cm | 120.4 | The skin holds. The kneecap is a pointed wedge with straight facets |
| Knee bent 120, thigh forward | 120.5, the same | 120.5 | The same |
| Knee bent 120 on the floor | **15.4: the foot meets the floor** (8.1 with the thigh forward) | 16.0; 8.0 | Not used: the pelvis has to be raised for the pose |

- **The fingers are never posed.** In stance, in the air, at the floor and lying they are the modelled hand: fingers apart and
  slightly bent. By the traces the 33 evaluated finger bones of each hand turn by 0.0000 degrees against the hand over a
  fall backward, a fall forward, a get-up, 32 s of standing and a get-up that is held, while the hand itself turns by up to
  173. In the fall forward two fingers of the lying hand are not to be seen: they are in the floor. The hand is one body and
  a posed finger will have none either.
- **The plan was wrong about the skin: the MetaHuman's own correctives are running.** The assembled body mesh has Epic's
  `ABP_Body_PostProcess` as its post-process blueprint, nothing of ours switches it off, and its bones move: with the wrist
  at 70 `lowerarm_twist_01` turns 55.9 degrees and `lowerarm_twist_02` 14.0, with the arm raised `upperarm_out` 50 to 97,
  with the knee at 120 `calf_knee` 39. Switched off for a trial, the shoulder collapses and the forearm twists at the wrist
  alone (`n8_correctives_with_without.png`, lower row). It moves the bodies by rounding only (0.003 cm after half a second of standing, 0.006 after ten; every hash
  changes, so it must not be switched on or off in passing). What it costs a frame was not measured.
- **But the correctives follow the pose the muscles are asked for, not the body.** A bone without a body keeps the turn the
  animation gave it. Where the body is elsewhere than its target the skin is corrected for the target: in `getup_back` the
  right wrist is twisted by 21.7 degrees and its twist bone by 3.0 (17.4 would fit); lying forward a knee at 16 degrees wears the
  corrective of one at about 56 (in proportion to the 39 degrees at 120). An arm lifted to 110 degrees by a grab has `upperarm_out` at 0.0 (`n8_grab_lift.png`): at
  that height it still looks right, higher it would be the collapsed shoulder of the lower row.
- **The facets are the mesh.** The body was assembled at the quality MEDIUM: 8568 vertices at its best level of detail,
  3974 and 1239 below; 145 of its 342 bones (the fingers' `bulge`, `side`, `mcp`, `pip`, `dip`) carry no skin there and are
  not evaluated. A pointed knee and a faceted fingertip are that, not a joint.

**What follows for N8.** N8 C as planned (the twist shared out natively) is **not needed**: it is done already. What is left
of it is smaller and belongs to M5: the correctives from the body's own pose (a pass after the physics blend, native, the
same 0.8 and 0.2 for the forearm's twist; or Epic's blueprint on a second, hidden pose), and a higher assembly quality with
its cost in the frame measured. It becomes urgent only if M4's grab lifts arms over the shoulder in the headset. **N8 B
starts with `rest` and the stance**, since that hand is seen most and the present one is a spread one: one pose for all
states behind a switch (hash of bodies unchanged but for rounding), looked at on `n8_hand_stance`; then `splayed` with
`flat` for the floor, where the picture of fingers in the floor is the test. The `*_half` finger bones are evaluated, so
the post-process blueprint should fill the knuckles of a posed finger as it does now for wrists (not tried).

**A jump told from a fast motion.** `command_step_max_nm` counts both. The new `control.pose.command_jump_max_nm` (and
`target_jump_max_deg`) is what is left of a change after its neighbours have explained it: the command is where the two frames
before were heading, or the frame before is where the two after come from (`FSandboxMotionStats::CommandJumpNm`). Known answers:
a unit test of 15 cases (a jump, a motion that goes on, begins and ends at 30 degrees a frame, a jump within a motion, no
strength, strength coming up, dropping, fading); `measure_jumps` 30.0 degrees and 20.9 N*m; the new `measure_fast_target`, the
same 30 degrees in 0.1 s: the largest change 4.98 degrees and 3.48 N*m, the jump 1.23 degrees and 0.86 N*m (the smooth step's
own second difference). Seven breaks on purpose were all caught (the whole change taken; not looked behind; not looked ahead;
strengths that do not go on; frames in the wrong order; a frame late; half a strength). Its limits: a jump over two frames
reads as a motion; the target's own speed in the damping is left out, as in `command_step_max_nm`; the last frame is not judged.
Over the 86 scenarios that count for C25 the largest jump is 367 N*m (the largest change 399); 55 scenarios have one above
100 N*m; standing 0.1 N*m.

**Where the jumps come from.** All jumps above 60 N*m in the suite's runs (the `tune_*` ones among them), by what happened in that frame (from a per-frame log that
was read and removed; the log left all hashes alone). `stand_foot_moved` (298 N*m) is the scenario's own: its second
`foot_target` moves the point by 21 cm, taken as given.

| Cause | Joint-frames | Scenarios | Largest |
| --- | --- | --- | --- |
| A landing: the swing leg's doubled stiffness and damping dropped in one frame | 303 | 43 | 363 N*m |
| The first time a leg is placed at all (`leg_extension_max`, whole from the first frame) | 280 | 53 | 223 |
| The frame after a landing (the settling's height; the foot's level) | 161 | 39 | 282 |
| Getting up, not looked into (the clip at 1200 N*m/rad; hits and their flinch) | 159 | 13 | 224 |
| In a stagger, other, not looked into | 124 | 33 | 271 |
| **The frame after a lift that follows another step (cases a and b)** | 95 | 24 | 367 |
| A fall begins: legs let go | 44 | 24 | 219 |
| A fall begins: the state's strengths | 41 | 13 | 260 |
| A hit: the flinch's weakening and pose offset | 36 | 13 | 213 |

Three of these were proved as the cases below were, each alone on one landing or first step of `push_chest_80_steps`: the landing's
strength eased out instead of dropped (thigh, calf, foot 70, 118, 159 N*m to 10, 6, 3); the settling begun at the height the leg
was solved for, 5 cm above the bone, and the level eased out (118, 203, 79 to 4, 5, 7); the reach eased in with the placement
(the standing leg's calf 146 to 5, the swinging one's 118 to 97).

**Case a, `push_chest_80_steps`, thigh_r at 3.444 s: a defect.** 27.6 degrees in a frame, 26.8 of them a jump (213 N*m; the
calf 278). The leg's placement was on its way from the frame it had been solved in to the next (`EFrame::Stance`, begun at the
landing of 3.300 and 18% done: 0.175 of `balance.anchor_follow_s` 0.5 s). `StartSwing` calls `PlaceFoot` without `bFromLast`, so
the new change starts from the **whole** Stance frame, 21.5 cm from where the leg had been solved a frame before, and not from
there. `StartOffset` does its work: the point goes on without a step (0.00 in the log); it is the frame that jumps. It breaks
the rule that a leg's target never jumps ("every change of a foot's point or of the frame its leg is solved in glides from what the
leg was solved for last"). Proof: the one argument, `bFromLast`, switched on from that moment on with everything before it
identical, and the thigh's jump is 1.7 degrees (42 N*m, what the easing strength gives), the calf's 1.5 (20). The change of
the joint's speed in that frame falls from 7.9 to 1.1 rad/s. The jump costs more than it reads: the swing leg follows its
target's speed, and 27 degrees in 11 ms is a speed, so by the spring law the drive asks for 1900 N*m and is cut at its cap.

**Case b, `push_thigh_200_stumbles`, foot_r at 3.756 s: three defects in one frame.** 399 N*m, 365 of them a jump. The left foot
lands at 3.733, the right lifts a frame later, its change of frame 0.1% done. (1) The same lost frame as in case a (18 cm): all of
the thigh's jump (265 N*m) and the calf's (225), and a part of the foot's. (2) The foot is held level with the world from the first
frame of a swing (`EndFrame`), where on the floor it is level with the pelvis, which is tilted by 14.2 degrees. (3) It is held to its ankle's
range with the whole weight from the first frame (`EndLimitWeight` 0 to 1), where the landing eases it out. With each switched
alone the foot's jump is 333, 333 and 140 N*m; with the first and the second 241, the first and the third 137; with all three
**26 N*m** (thigh 13, calf 9). The swing that follows is a fast motion, not a jump: at 3.500 s of
case a the thigh's command changes by 64.7 N*m in a frame and 4.4 of it are a jump.

**Case c, `getup_back`, upperarm_l at 6.089 s: as intended, in the torque; not in the motion.** `StartGetUp` sets the clip's pose
whole and the strength from nothing (`ClipHold.Share = 1`, its comment says so). The target turns 127 degrees with a muscle of
1.2 N*m/rad: 2.7 N*m are asked for, the jump is 0.0 N*m. The frame before, the resting body loses the 10 to 40 N*m/rad it had
(spine 11.7 N*m, thigh 8.7): `max jump` 13. But a limp arm is light to turn: at 11 N*m/rad and 122 degrees of
error it is at 11.2 rad/s (640 degrees a second) within three frames (13.3 at most), 65 N*m within 0.12 s. The strength "from
nothing" is 0.6 s; the arm has gone two thirds of the way in 0.2.

**What a correction would move.** Each trial on the whole suite and on the push matrix. All passed the matrix: the rows of 10, 30
and 80 N*s end as they did (a few of the 80s with a step or a stagger more or less), the row of 200 N*s, on the thighs above all,
changes sides in a few runs. Mirror-image pushes that end differently: 5 today.

| Trial | Scenarios that move | Fail | Matrix | Mirror pairs |
| --- | --- | --- | --- | --- |
| 1 the frame glides from the last (case a) | 43 | `grab_getup_held` (ends lying) | 6 differ | 4 |
| 1, 2, 3 level and limit eased in too (a and b) | 54 | `strike_kick_thigh` (a stagger at the run's end) | 7 | 2 |
| The landing's strength eased out (`step_ease_s`) | 43 | `palm_close_on_chest` (a grip on the body that was counted is not) | 8 | 4 |
| The settling from the solved height | 43 | none | 7 | 3 |
| The first placement's reach eased in | 54 | `prop_fall_on_box` (5.6 s to rest, 5 allowed), `prop_getup_on_plate` | 11 | 5 |
| All of these but the reach | 54 | `effector_hold_chest` (ends in a stagger, its feet 73 cm apart) | 11 | 1 |
| All of them | 54 | `strike_punch_forearm` (10 steps for 8), `strike_punch_torso` (8 for 7) | 11 | 1 |
| The clip's pose blended in over `poses.blend_s` (case c) | 14, all with a get-up | none | not run | - |

Jumps above 60 N*m in the suite: 1231 joint-frames in 56 scenarios today, 1052 with the three of a and b, 905 with the landing's
strength, 889 with the reach, 694 with all but the reach, 490 in 50 scenarios with all of them. In two trials the largest jump of the
suite is larger than today's (451 N*m in `getup_held` with the landing's strength, 414 in `getup_left` with all): these runs go
another way, and what jumps in them was not looked into. Each correction has to be looked at before it is kept. On the stand (seeds 1 to 3): with a and b 150 of 150 stand (149 today), 130 at the first attempt (132);
with the clip's pose blended in 146 stand, 132 at the first attempt, and four of the `getup_*` are 0.0 to 1.1 s quicker: within what
changes by itself, no gain. The fastest joint in the first 0.67 s of a get-up falls from 13.3 to 7.3 rad/s (back), 11.6 to 6.1
(left), 7.0 to 5.2 (right).

**Recommended** (none of it done; a and b, and c, were decided so by the owner on 25 November 2026; C25's figure is set in N2 after the repair):

- **a and b: repair them together, as the first thing of N2 and before its step 2,** which needs the weight torque on legs whose
  targets do not jump. The correction is small (one argument; two easings over `step_ease_s` that exist for the strength) and each
  is a defect by the project's own rule. It moves 54 scenarios, so it has its own baseline, three seeds of the stand and the
  push matrix, and the one scenario that then fails (`strike_kick_thigh`) is read, not loosened. Films of
  the trials were not made. Alternatives: (2) now, as a step of its own
  beside M4, at the price of a new baseline in the middle of M4; (3) the frame alone (a) now and level and limit with N8 D,
  since the toes change the foot anyway.
- **c: leave it,** count it in C25 by the torque (0) and not by the target (127), and take the arm's whip up with N7's gathering
  and N3's tone, where the get-up begins from a resting tone and not from nothing. Alternatives: (2) blend the pose in now: no
  gain on the stand and the arms are slower; (3) a limit on how fast a muscle's target may turn while its strength comes up.
- **The other causes** (the landing's strength, the reach, the settling, the fall's strengths) are found, not asked for: they are
  the same kind of thing and are the larger part of C25. They belong into the same step as a and b, one by one.
- **C25 is to be read from `command_jump_max_nm`.** Its proposed limit cannot be "today's largest", since today's largest is a
  defect: proposed 60 N*m after the repair, looked at again then. (Said too soon: 60 lies in the gap between the scenarios without a step, 50 N*m at most, and those with one, 100 at least, but no trial comes under it. With all the corrections together 38 scenarios still jump by more than 100, 414 at most, a hit that weakens a muscle among them, which is by design.) `target_step_max_deg` 127 is case c and is no jump.

**Not established.** What jumps in the 124 joint-frames of a stagger and the 159 of a get-up that have no name above; whether the correctives can be evaluated from the body's pose without an Animation Blueprint of ours.

### The natural body, N8 A: the head as a body, and the wrist (night of 25 to 26 November, the owner away; for the owner to confirm)

**Note of 26 November 2026:** Taken on 26 November 2026 ("N8 A taken"): the head is `npc_body.json` / `PA_SandboxNPC` (the names `npc_body_head.json` and `PA_SandboxNPCHead` used below no longer exist), the old body is `PA_SandboxNPCNeck`, and `SANDBOX_HEAD_BODY` now names `neck_01` for an asset without a head; the wrist was not taken.

Two assets beside today's, which is unchanged (117 scenarios with the hashes of `hold`, also after it was made again by the changed generator): `PA_SandboxNPCHead`
(`Specs/physics/npc_body_head.json`) and `PA_SandboxNPCWrist` (`npc_body_wrist.json`). No scenario of before uses them; two are new (`head_slap_cheek_turns`, `measure_head_turn`).
Every tool runs on another body without a file being touched: `SANDBOX_PHYSICS_ASSET=PA_SandboxNPCHead SANDBOX_HEAD_BODY=head` (what a scenario does to `neck_01` it then does to `head`).

**What was made.** The head is a body of 4.44 kg on a joint of its own, the neck one of 1.11 kg (the 6.94% split 80 to 20, **from memory**: McConville's volumes; three pages with the figures
could not be read, so the split is an open check). The range of the neck is shared between the two joints (nod 40 and 20 forward, 50 and 25 back, sideways 35 and 10, turn 35 and 45). The head has a face: a cranium, a forehead and two cheekbones,
the cheekbones the widest at the front (fitted to the mesh from two pictures by scale: 22 cm high, 19 deep, 15 wide; before it was a capsule 18 cm wide whose axis passed the joint at 1 cm). A pose's
angles for the neck are shared out between the two joints by their ranges, so `poses.json` is as it was. The centre of mass of the head is 3 cm ahead of its joint and 6.5 above.

**What held the head back from turning was the world, not the neck.** Over necks of 40 to 1 N*m/rad the same slap turned the head by 6 to 15 degrees: a figure that does not move with its setting. The assist's hold on the head
(`assist.head` 10 N*m/rad with 0.2 s) is a damper of 2 N*m*s/rad from the world on a head of 0.02 kg*m2. Without it today's own body turns its head by 7 to 9 degrees (4 to 6 with it) and the new one by 14 to 20 (7 to 9). The note of 25 November
("the head's neck muscle is most of it and the round shape the rest") left the assist out.

**The limits, written before the runs** (`Saved\Recon\window_plan.md`), on the body with the head, `assist.head.angular_nm_per_rad` 0 and `muscles.neck.damping_s` 0.05 (0.1); the neck's stiffness as it is, 40, which is 20 for the two joints together:

| | Limit | Today | Head | |
| --- | --- | --- | --- | --- |
| A1 | Standing: drift, feet's turning speed, assist within a tenth | 0.907 cm, 0.108 rad/s, 0.0063 | 0.927, 0.090, 0.0063 | met |
| A2 | Neck and head stretch 0.5 cm at most under a kick | 0.14 | 0.15 | met |
| A3 | A slap on the cheek at 6.2 m/s turns the head 20 degrees | 4 to 6 | 17.7 with the fingers pointing forward, 25.8 pointing back, 23.2 pointing up | **met in two ways of three** |
| A5 | Push matrix | 160 | 160 | met |
| A6 | Knockdowns by a blow and by hand | 8, 8 | 8, 8 | met |
| A7 | Stand of 150 falls: 147 stand, 129 at the first attempt | 149, 132 | 150, 134 | met |
| A8 | Grab pulls (with `head` for `neck_01`), falls at most 3 | 93, 2 | 93, 0 | met |
| A9 | Physics step 0.05 ms more at most, alone on the machine | 0.555 ms | 0.568 | met |
| A10 | Suite | 117 | 109 pass, 8 read below | |

- **The test of the slap was changed, and this is how:** its path ended at x = 5, which the narrower head hardly meets (0.5 to 2 N*s); it now ends 4 cm inside the surface (x = 3), as it did in the old head. And the hand is held three ways: in the old one
  the fingers point forward past the face, and the plate meets it with its rear edge (a lever of 1 to 4 cm). `head_slap_cheek_turns` is the one with the fingers back (23.2 degrees by the new `metrics.head_turn_max_deg`). The criterion by the old way of holding the hand is missed by 2.3 degrees.
- A damping of 0.025 meets it in all three (20.3, 30.3, 27.4) and a kick then turns the neck at 31 rad/s (30 allowed); a neck of 20 N*m/rad meets it (22.4, 34, 30) and hangs 8.6 degrees forward under the head's weight (1.9 with 40). With the weight carried (N2) the soft neck need not hang.
- **The eight scenarios:** `effector_hit_head`, `palm_slap_head` end at x = 5 (above); `measure_drop_head_first`, `measure_push_neck` name `neck_01` or have the one joint's answer (the second: 11.4 N/cm for two joints, 15.4 for one); `strike_punch_shoulder` (30.7 rad/s against 30, one hit for two: the fist comes back 1 cm further off); `room_movable_fall_gets_up` ends in its second get-up;
  `tune_live_file`, `tune_live_override` (a body whose legs are switched off: the neck at 36 to 38 rad/s against 30; 16 today). The light neck is whipped when the head strikes the floor: 37 to 46 rad/s in falls where today has 16 to 36. Four times its inertia cures the last two (15 and 18) and costs the stand 150 to 148 and 134 to 129 and a knockdown its 3 s: not taken.
- The knockdown's 35 N*s holds: fists at 3, 4 and 5.5 m/s are felt as 13.6, 30.4 and 38.9 N*s (13.8, 29.4, 36.3 today), a kick as 56 (48). `effector_hit_head` was an edge: 35.3.
- **Seen on the films** (`Saved\Films\n8a_slap_today`, `n8a_slap_head`, `n8a_kick_*`): the head turns away and comes back over half a second. It tilts more than it turns (30 to 35 degrees against 23), and nods down by the flinch's pose as it does so.

**Recommended:** take the head (a new baseline; the eight scenarios set anew); the hold of the world on the head goes with it. **The wrist is not recommended**, by A7:

| Stand, three seeds | stand | first attempt |
| --- | --- | --- |
| Today | 149 | 132 |
| Wrist limited in the hand's own frame, bent back 85 (`PA_SandboxNPCWrist`) | 148 | **113** |
| The same frame with today's 60 | 148 | 112 |
| Today's frame with 85 | 148 | 134 |
| Head and wrist together | 145 | 120 |

It is the frame, not the 85 degrees, that costs twenty first attempts (each tried alone). Why was not looked into; the hand is then asked by the clips to lie flat, bent back 60 to 85 under a turned forearm, where today it is asked for what the 25 degrees of the sideways range let through. The frame is made by a second joint between
the two bodies that holds only the swings, with the bodies the other way round; a single joint with the bodies exchanged has no projection and stretched 0.80 cm with the body hanging by a hand (0.25 today, and 0.25 with two joints).

### The natural body, N8 B: fingers (the same night; for the owner to confirm)

`FSandboxFingerRig` (native, no bodies): a curl for each finger and the thumb and one spread, 24 values in `poses.fingers.*`, `poses.fingers.weight` 0 (off). Rest standing and lying, half a fist while flinching or staggering, spread in a fall, flat under a hand that the world pushes on while the body is not lying; eased over `blend_s`.

- **The hashes of the bodies are the same in every scene tried, with the weight at 0 and 1, and the bones' are not** (seven close-ups). Off, everything has the hashes of `hold`. Unit test: the two hands mirror each other, a curl goes to the palm, a spread parts the fingers, a pose of zeros turns nothing.
- `Saved\Films\n8b_fingers_stance.png`, `n8b_fingers_fall.png`: above as modelled, below posed. In the stance the fingers hang closer together and bent a little more toward the palm. The first values (35 to 52 degrees) were a cupped, mannered hand and were halved.
- **What is bad and stays:** a hand that lies on its edge has two fingers in the floor, as before; the pose of rest does not bring them out, and the pictures of a hand in the air and at the floor differ little from the modelled hand. A finger that gives way to the floor needs a trace or a body, neither built.

### The natural body, N2 first: the jumps of a step (decision 13; the same night, for the owner to confirm)

**Note of 26 November 2026:** Decided on 26 November 2026 ("N2: four of the jumps' repairs on"): `lift_smooth`, `release_smooth`, `land_ease_s`, `fall_ease_s` on; `settle_smooth` off; `reach_smooth`, `stagger_brake_follow_s` and `lift_frame_s` not taken; C25's baseline 439 N*m, not 360.

Today's body, every correction behind a value of `Config/Tuning/body.json` that is 0 (as it was): with all of them at 0 the 117 scenarios have the hashes of `hold`.
They are the reconnaissance's trial corrections made tunable, and two more found tonight:

| Value | What it puts right |
| --- | --- |
| `balance.lift_smooth` | Cases a and b: at a lift the leg's frame goes over from the one it was last solved in; the foot's level and its ankle's limit come in over `step_ease_s` |
| `balance.land_ease_s` | The swinging leg's strength goes out over this time once it has landed, instead of at once |
| `balance.settle_smooth` | A landed foot settles from the height its leg was solved for |
| `balance.reach_smooth` | The share of its length a leg reaches comes in with the placement's weight |
| `balance.release_smooth` | A leg let go as a fall begins goes back to the pose from its last target, held in the body |
| `balance.fall_ease_s` | The muscles go over to the strengths of a fall over this time |
| `balance.stagger_brake_follow_s` | **New:** the stagger's brake on the legs' frame (`stagger_damping_s` times the body's speed) follows the speed with a lag. Taken at once it moved the frame by 15 to 40 cm in the frame after a blow: 276 of 517 jumps in a stagger were in its first three frames |
| `balance.lift_frame_s` | **New:** time over which a lifting leg's frame goes from the upright one to the body as it is (0: `step_ease_s`, 0.05 s). After a blow the two are 25 degrees apart: 6 degrees a frame, and then a stop |

**Today's figure is not N1's.** Over the suite the largest jump is 429 N*m (`grab_pull_standing`, foot_l), 58 scenarios of 96 have one over 100 N*m and 39 over 200; N1's 367 was measured before the hold was felt and the blow was carried. (Corrected on 26 November 2026: 367 N*m is the reconnaissance's figure; N1's row of C25 has 399 N*m.)

Limits set before the runs (`Saved\Recon\window_plan.md`): J1 the largest at most 367; J2 the push matrix 160 of 160; J3 the stand 147 of 150 and 129 at the first attempt; J4 a scenario that fails is read; J5 fewer than 58 scenarios over 100 N*m.

| Set | Largest, N*m | Over 100 / 200 | Fail in the suite | Matrix | Stand |
| --- | --- | --- | --- | --- | --- |
| Today | 429 | 58 / 39 | - | 160 | 149, 132 |
| lift (a and b) | 508 | 58 / 39 | `strike_kick_thigh`, `strike_punch_belly_steps` | 160 | 150, 130 |
| land | 411 | 58 / 26 | `palm_close_on_chest`, `prop_step_on_plate` | 160 | 149, 131 |
| settle | 376 | 58 / 37 | `grab_pull_standing` | 160 | 149, 132 |
| reach | 374 | 58 / 39 | - | 160 | 150, 132 |
| release | 429 | 58 / 41 | `strike_kick_torso_through_falls` | 160 | 149, **126** |
| fall ease | 429 | 57 / 51 | `prop_fall_on_box` | 160 | 148, 130 |
| brake | 349 | 59 / 43 | four | 160 | 148, 133 |
| **all but reach** | **352** | **56 / 15** | `effector_hold_chest`, `effector_shove_knockdown_gets_up` | **160** | **149, 130** |
| all | 414 | 42 / 33 | - | 160 | 148, 132 |
| brake and all | 394 | 44 / 36 | two | 160 | 149, 130 |
| brake, all but reach | 503 | 56 / 38 | three | 160 | 150, 131 |
| brake, all, lift frame 0.15 s | 303 | 38 / 17 | three | **158** | 147, **124** |
| brake, all but release, lift frame 0.15 / 0.1 s; brake, all, lift frame 0.1 s | 364, 311, 298 | 38 to 44 | two to four | **158 to 159** | 149, 126; 148, 129; 150, 128 |

- **No set meets every limit.** "All but reach" meets J1, J2, J3 and J5 and changes two scenarios: `effector_hold_chest` (a hand that keeps pressing on the chest) ends in a stagger with the feet 73 cm apart, where today the feet are brought together at 8.3 s; and **in `effector_shove_knockdown_gets_up` the body falls a second time after it has got up**, where today it staggers and stands. The second is a worse behaviour, not a different one.
- The figures move both ways with each correction: a correction takes the jump it was made for away and the run goes another way, with jumps of its own elsewhere (`lift` alone: 508 in `strike_punch_forearm`). The largest is a figure of one frame of one run; the count over 200 N*m says more (15 with "all but reach", against 39).
- **What is left,** by a temporary log of every jump over 60 N*m (taken out again): in a stagger, a swing under way (109), the frame after a landing (64), the first frames (63); a fall begins (48); a hit's flinch (25). The largest that remain are a lift's frame (the frame that is let go of over 0.05 s), which a longer glide takes away (303) at the price of two cells of the push matrix and the stand's first attempts.
- **Recommended, for the owner to confirm:** "all but reach" behind its switches, with the two scenarios looked at on films first; C25's limit then proposed at 360 N*m for now (decision 15 asks for it after the repair), 60 stays the aim and is far: 56 scenarios still have a jump over 100 N*m. Nothing is switched on.

**After an independent reading of the code (the same night).** The reader found no path that changes a run with the files as they are, and twelve defects behind the switches. Put right, each neutral (the hashes of `hold`):
the fall's ease now starts from what each muscle had as the fall began, the swinging leg's doubled strength included (from the groups' values it dropped the swinging leg to half its stiffness in one step);
a leg that lifts again while its landing still eases keeps the larger share; a foot let go keeps the orientation it was held to while its weight goes; a brace ends with an upright body; a stiffness factor of 0 no
longer takes the damping with it; an arm's ranges come in with its target's weight; a unit test that could not fail is gone. **"All but reach" again, after them:** the largest jump 352 N*m, **7 scenarios over 200 N*m
(15 before the repairs, 39 today)**, 55 over 100; the matrix 160; the stand 148 and 131; the suite fails `effector_hold_chest` (as before) and `room_movable_fall_gets_up`, whose first get-up is given up and
whose second stands at 18.6 s, the run ending in the step that brings the feet together (19.2 s); `effector_shove_knockdown_gets_up` passes now. The recommendation stands, with these two to be looked at. The film of the first (`Saved\Films\n2_hold_chest_today` and `n2_hold_chest_abr`, from the front): today the body steps
back from the hand and by 8.5 s has its feet together; with the repairs it goes on stepping, turns to a side and ends the run in a step with its legs crossed. That is worse to look at, and is the thing to
weigh before switching the set on.
Left as they are, known, all behind switches that are off: `balance.lift_frame_s` blends a first placement's weight, not a frame; the weight torques count no push from a thing that moves (a prop, movable
furniture, a hand), so a body standing on one carries none of its weight; the ankle as a torque and the assist's trunk share switch with the state and do not ease; a lifted foot keeps its push for about 0.1 s;
`StopFallAim` lets the hands go at a point of the world; the weight torque's cap ignores the joint's own factor; a frame `UpdateWeight` returns early in counts two frames' pushes next time.

### The natural body, N2 step 1 and 2: the weight carried by the joints (the same night; for the owner to confirm)

`FSandboxWeightTorques` (`SandboxBody`): the torque each joint needs to carry what hangs beyond it and what the world pushes on it, and the weight's share between two feet by where the
centre of mass stands over their soles; two unit tests with worked answers (an arm held out, a leg under a push, a share between soles). The controller collects what the world pushes each
body with from the solver's own contact points after every substep (as N1's centre of pressure does), and gives each muscle `muscles.<group>.weight_share` of its torque; `muscles.weight_follow_s`,
`muscles.weight_legs_by` (1: the feet's push by place, not as measured, which would integrate the spring's error), `balance.ankle_as_torque` and `assist.internal_share` (the ankle strategy and the assist
as a push of the floor at a shifted place, bounded by the soles and friction). All 0: the hashes of `hold`.

**The torque does not arrive as it is worked out, and this is not solved.**

- Put on the child and the opposite on the parent (`AddTorqueInRadians`, as the plan said), an arm held out at 20 N*m/rad was held up 1.6 to 1.7 times over (its spring then pushed it down by 5 N*m). With
  `p.Chaos.Solver.InertiaConditioning.Enabled=0` the spring was left with 0.5 N*m of 8.3: the solver's joints work with an inertia conditioned for stability (a rod jointed at its end gets 1.5 times its own)
  and a torque from outside is integrated with the body's own. Switching the conditioning off for the body's own bodies while the run goes (`FBodyInstance::SetInertiaConditioningEnabled`) changed nothing:
  the solver does not take the change.
- Asked of the muscle's drive instead (a turn of its target by torque / stiffness, and, tried first, a turning speed of torque / damping), the arm's weight reaches the shoulder whole when the arm is held
  forward and by 65 to 75% when it is held out to the side (with or without a twist): the cause was not found. That is what is built now, because it never gives more than is worked out.
- **On the whole set, as it is:** all four groups at 1 with today's stiffness: 14 scenarios of 119 fail, the push matrix fails, the stand is as today (149, 132). With the ankle strategy as a torque and
  the assist given from inside as well (`balance.ankle_as_torque` 1, `assist.internal_share` 1): 41 fail and 91 of 150 falls end standing. **Not usable as built; everything stays off.** What would be next: the
  delivery first (why the drive takes a turn of its target about one axis whole and about another by two thirds), then the legs on their own, then the assist; the softer springs of step 4 come only after that.
  Measured again at 03:05 with the arm's spring at 10 N*m/rad instead of 20 (`Saved/Recon/deliv`): held out by its swing1 (48 degrees) the spring is left with 3.0 N*m of 6.9 (56%
  delivered, 65 to 75% at 20 N*m/rad); held by its swing2, either way, with 0.5 of 3.1 and 0.2 of 4.9 (85 and 97%). The share falls as the stiffness falls, and the turn of the target that carries
  the torque is torque / stiffness: 0.69 rad (40 degrees) here. The next thing to try is that turn's size: a turn that is not small, composed with the sag the joint has, is no longer the torque it
  stands for (the velocity target, or the turn split over more stiffness, would tell).
  **Tried at once, and it is not that: the delivery was never short; the figure was.** At 20, 40 and 80 N*m/rad the spring's figure is left with 3.2 N*m every time (63 to 66%), and at 24, 48 and
  70 degrees of swing1 with 2.6 to 3.4 (65 to 67%): it moves with neither the stiffness nor the angle. Where the hand ends says otherwise: an arm at 1000 N*m/rad and no weight torque holds it at
  128.7 cm, the arm at 40 N*m/rad with the weight torque at 128.8 and at 80 at 130.5, against 118.2 at 40 without it. The weight is carried whole. The spring's figure
  (`effort.joints.<joint>.torque_final_nm`) is the spring law between the body and the skeletal target, and the 3.2 N*m it reads is not a sag that moves the hand (a turn about the arm's own axis
  would not, with the elbow straight); what it is was not found. **So "65 to 75%" above, and the claim in `CLAUDE.md`, were the measure's**, and N2's next question is the 14 scenarios that fail with
  every share at 1, not the delivery.
- **Then the 14 (03:10).** Four were falls that did not come to rest within 3 s: a body that is not upright still had the weight torques of its arms, spine and neck (only the legs' went with the
  posture), and they held a fallen body's limbs up. Now every group's share goes to 0 while the body is not upright (neutral with the shares at 0: the hashes of `hold`). With all four shares at 1:
  **9 fail** (`measure_arm_held_out`, `measure_cop` and `measure_push_neck` hold today's body's known answers, which the weight changes on purpose; `pose_foot_place`, whose pinned pelvis's
  hold is no push the weight torques count; `push_chest_80_steps` and `stand_foot_moved` (the stance after steps); `palm_close_on_chest`, `strike_punch_shoulder`, `room_movable_fall_gets_up`);
  the push matrix **158** (30 N*s from the side on the neck and the chest: two steps); knockdowns 8 and 8; the stand 147 and 130. Still not usable; what is left is how the body stands, not how it falls.
  Split by group: the arms, spine and neck alone fail five (two `measure_*`, `effector_hit_head` falls, `palm_close_on_chest`, `strike_punch_shoulder`) and keep the matrix but for the cell on a
  threshold (159); the legs alone fail five (`measure_cop`, `palm_close_on_chest`, `pose_foot_place`, `room_movable_fall_gets_up`, `stand_foot_moved`) and turn the one step after 30 N*s from the
  side (270 degrees, on the neck and the chest) into two (158). Today's matrix has five mirror-image pairs that end differently, the weight of the legs four: the side's second step is not a left and
  right mistake of the weight; the body is not symmetric today either. `palm_close_on_chest` fails with either half.
### The natural body, N3: muscles joint by joint, and the tone (the same night; for the owner to confirm)

Built, all neutral (the hashes of `hold`): `muscles.joints.<kind>.stiffness_scale`, `damping_scale`, `max_torque_scale` for twelve kinds of joint (step 1); the tone, `tone.<level>.<group>.stiffness` and `damping`
for limp, relaxed, ready and braced, each group's standing level (`tone.standing.<group>`, 2 = ready), `tone.rise_s` 0.05 and `tone.fall_s` 0.4, `RequestTone` and the scenario event `tone` (step 3); a brace after a hit
(`tone.brace_from_ns`, after `brace_delay_s` 0.08, for `brace_hold_s` 1) and a daze after a hit on the head below the knockdown (`tone.daze_*`, legs and spine) (step 5). Every level is 1 in the file: today's body.

**Step 2, the caps from the published peak torques (stronger direction; as factors on the group's cap), limits set before:** matrix 160, knockdowns 8 of 8 both ways, stand 147 and 129, grab pulls 93.

| Caps | Matrix | Knockdowns | By hand | Stand | Pulls |
| --- | --- | --- | --- | --- | --- |
| All: hip 0.71, knee 0.99, ankle 0.45, shoulder 1.04, elbow 0.64, wrist 0.31, neck 1.04 | 160 | **7** (forward: rest 3.6 s) | **7** | 149, **121** | 93 |
| Legs and neck | 160 | **7** | **7** | 150, **124** | - |
| Arms | 160 | 8 | 8 | 148, **124** | - |
| Shoulder and elbow (not the wrist) | 160 | 8 | 8 | 149, **127** | - |
| Ankle and neck | 160 | **7** (rest 4.3 s) | 8 | **146**, 129 | - |

**C5 is not met within the limits**, and nothing is changed: a man's ankle (112 N*m) lets the forward knockdown come to rest later than 3 s, and every set of weaker arms costs the stand three to eleven first attempts (the get-up asks the arms for more). The plan's fallback (the wrist keeps a higher cap) is the "shoulder and elbow" row: 127, two under.

**The tone, with levels proposed tonight** (limp 0.1; relaxed spine 0.6, neck 0.5, arms 0.25, legs 0.8; braced spine 1.5, neck 2, arms 2.5, legs 1.25; damping 0.3, 0.7 and 1.3), by `-Tune`:

- **C6** (`Tools\compliance.ps1`, 20 N from the front, the whole body at one level): forearm 16.9 cm relaxed, 8.9 ready, 5.0 braced: **3.4 times, met**; chest 1.51, 1.32, 1.18: **1.3 times, not met**. The chest's give is the whole body's lean on its legs and ankles (the pelvis goes 3.4 cm), which a tone of the trunk hardly changes.
- 30 N*s on the chest: relaxed, two steps and a stagger, the chest 28 cm; ready and braced, no step, 18 and 17 cm.
- Braced and let go: the largest change of a strength in a frame is 0.26 (the tone's own `rise_s`), against the flinch's 0.7 of today. No jump of a command above 39 N*m.
- Step 5's push matrix (the levels above, `tone.brace_from_ns` 10): **159 of 160**, the one cell the cell on a threshold of `CLAUDE.md` (30 N*s on the head from behind and a side: three steps);
  with the daze as well (`tone.daze_from_ns` 15): 160. Today: 160. The braced body does not step less: 6 steps at 30 N*s in both.
- **The daze does not do what the plan wants:** 25 N*s on the head and 30 on the chest 2 s later is two steps with the daze and two without (`tone_dazed_steps_more` would fail).
- Relaxed arms in the stance (the arms' and the neck's standing level 1): after 30 N*s the hands travel 12 cm against 20, **less, not more**: with the damping at 0.7 and the stiffness at 0.25 the arm is damped three times as hard for its spring. The relaxed damping wants to be about the stiffness's share (0.25 to 0.35). The film of a push of 80 N*s (`Saved\Films\n3_push80_today`, `n3_push80_relaxed`) shows no difference at that scale.
- Step 4 (arms and neck relaxed while standing) needs the weight carried (N2) to keep a relaxed arm where its pose has it; not taken further.

### The natural body, N6 step 4 in part: the head kept off the floor (the same night; for the owner to confirm)

**Note of 26 November 2026:** Measured again on the head body with the reach on 26 November and kept off by the owner ("N6, the reach on today's body").

The fall's poses already turn the neck away from the floor; with the falling state's neck at 40 N*m/rad they are not held. By `-Tune`, no code: `states.falling.muscles.neck.stiffness_nm_per_rad` 120,
`poses.fall_back.neck_01.swing1_deg` -60 (chin to the chest, -40), `poses.fall_forward.neck_01.swing1_deg` 50 (head back, 25), `poses.fall_side.neck_01.swing2_deg` -30 (the ear to the upper shoulder, 0).

| | Today | Head kept off |
| --- | --- | --- |
| C15, the head strikes the floor (0.5 m/s or more), by hand / by blows | 75% of 12 / 50% of 8 | **33% / 38%** |
| C16, the head's speed at the floor, 90th percentile, by hand / by blows | 4.32 / 2.82 m/s | 4.74 / 3.34 |
| C17, rest, energy, into the floor | 2.21 s, 25.5 J, 1.23 cm | 2.91 s, 28.4 J, 1.75 cm |
| Suite | 117 | 118 of 119 (`room_movable_fall_gets_up` ends in its second get-up) |
| Knockdowns; stand | 8; 149, 132 | 8; 147, 132 |

The head strikes the floor in fewer falls, and as hard as before in those where it does: straight forward the face meets the floor at 4.7 m/s, which the neck cannot prevent; that is the arms' work (N6 step 5, not built).
C16's 90th percentile rises because fewer, harder falls are left in it. **By N6's "Must hold" it does not hold:** the stand is two worse (147 against 149; the plan asks for no worse) and
`room_movable_fall_gets_up` fails, its second get-up given up. These runs were made before their limits were written down; they are judged by the plan's rule, which was there before. Nothing is changed;
the four values are information for the owner (films not made).

### The natural body, N6 step 1: the hand target (the same night; for the owner to confirm)

`hand_target` (a scenario event; `SetFootTarget` takes a hand as it takes a foot, `control.foot_targets` reports both) places a hand by the legs' two-bone placement, with three things only an arm has
(`FSandboxLimbTarget::bArm`, set by `SetLimbBend`, which only an arm's target calls; with no hand target every hash of `hold` is as before):

- the elbow goes back and out (the pole), and the plane of the bend goes over from the pose's own to the pole's with the weight: taken at once, the stance's elbow turned 22 degrees about the arm in the target's first frame (a target step of 21 degrees);
- the hand turns with its forearm: kept as it hung, the wrist of an arm held out was asked for 90 degrees, and pushing on its limit put the hand 30 cm from its point with stiff arms;
- the shoulder and the elbow are held to their ranges after the placement.

Limits written before the runs that count (`pose_hand_place`, plan): the hand within 3 cm of points in front, to the side and below; no joint 1 degree outside its range; no joint faster than 30 rad/s. The first runs
(before the three repairs above) were made before the limits were written.

| Arms (welded pelvis, idle off) | Front | Side | Below | Over the head (information) |
| --- | --- | --- | --- | --- |
| Stiff (3000 N*m/rad, the trunk too): the geometry | 0.5 cm | 0.5 | 0.4 | 0.8 |
| 400 N*m/rad (`strong`) | 1.2 | 3.4 | 1.1 | 1.9 |
| Braced (2.5 times today's) | 3.3 | 5.4 | 2.9 | 2.4 |
| Today's own | **7.2** | **8.6** | **5.9** | 5.0 |
| Today's own, the arms' weight carried (`muscles.arms.weight_share` 1, N2) | 3.1 | 6.6 | 2.9 | - |

No joint was outside its range by more than 0.07 degrees, none faster than 14.4 rad/s. **The geometry meets the plan's 3 cm; the arms at their own strength do not:** the miss is downward (6.7 of 7.2 cm in front), the arm's weight
on soft muscles. N2's weight torque takes it to about 3 cm in front and below, not to the side (its delivery through the drive's target is 65 to 100% by the pose, see N2). (Corrected on 26 November 2026: the delivery through the drive is whole; 65 to 75% was the spring's own figure; see "N2 step 1 and 2", the paragraph that says the delivery was never short.) `pose_hand_place` and `pose_hand_place_below`
therefore test the geometry, with stiff arms and trunk, and say so; a copy with the arms' own strength fails, as it should. Over the head the upper arm's target turns 17 degrees in one frame as the arm passes the line
of the shoulder (a twist; not a criterion point; to be looked at before a target above the shoulder is used).

### The natural body, N6 step 5: the arms reach for the floor (the same night; not met, off; for the owner to confirm)

**Note of 26 November 2026:** Measured again on the head body with the reach on 26 November and kept off by the owner ("N6, the reach on today's body"); the fall is R3's since 26 November (docs/active-ragdoll.md).

Built behind `fall.aim` (0 in the file: today's fall, every hash of `hold`): while the body falls and nothing holds it up, the fall's way is taken every tick (the pelvis's lean and the centre of mass's velocity, as
`ChooseFallWay` takes it once, smoothed over `fall.way_follow_s`) and each hand is sent by the hand target (step 1) to the floor `fall.reach_ahead_cm` from its shoulder along that way, its wrist
`fall.hand_height_cm` above the floor; until a hand or a forearm touches the floor the arms are at `fall.arm_reach_*` of their tone, after it at `fall.arm_yield_*`; the trunk on the floor ends the reach (the
targets are let go over `poses.fall_blend_s`). Steps 2 to 4 of the plan (the way as a blend of the sample's reach clips) are not built: the hands are targets on top of today's fall poses.

Limits written before the runs (F1 to F8). F5 as written (no joint faster than 30 rad/s in the knockdowns) is broken by today's body too (the neck at 36.1 rad/s after the blow at 90 degrees); it is judged as the plan
words it, on the arms, which stay under 30 but in one fall (the hand at 30 in `d4`).

| Variant (`fall.aim` 1 and) | C14 hand first, by hand / blows | C15 head strikes, by hand / blows | C16 head speed m/s | Most energy gained, blows / by hand | Stand |
| --- | --- | --- | --- | --- | --- |
| Today (`fall.aim` 0) | 86% / 80% | 75% / 50% | 4.32 / 2.82 | 25.5 / 30.3 J | 149, 132 |
| The file's values (reach 2.5, yield 0.3) | 57% / 100% | 75% / 50% | 3.87 / 2.78 | 36.5 / **76.4** | - |
| Reach 1 (no brace) | 29% / 100% | 92% / 50% | 5.40 / 2.60 | 27.6 / **42.3** | - |
| Reach 5, yield 1 | 86% / 100% | 58% / 63% | 4.05 / 2.45 | 37.7 / - | - |
| Reach 5, yield 1, with the head kept off (step 4's values) | 86% / 100% | **17%** / 50% | 4.25 / 1.87 | **41.0** / **83.1** | **148, 129** |

With reach 5, yield 1 and the head kept off: push matrix 160; the suite fails seven, six of them on `max_energy_gain_j` (`effector_push_chest_falls`, `grab_drag_fallen_one` and `_two`, `grab_overload_tears`,
`hit_chest_200_falls`, `prop_fall_on_box`) and `room_movable_fall_gets_up` (the head's values, as in step 4). More damping on the reaching arms (4 and 8) brought the energy by hand to 69 and 48 J and kept a blow's
fall from rest for 3.0 and 4.3 s.

**Not met: F2 (energy, rest) and F6 (the stand).** The energy is gained in the backward falls 0.01 s before a hand touches (`shove_270`: 83 J at 3.533 s, the hand at 3.544): braced arms swinging back to targets behind
the hips. The head is kept off the floor in many more falls by hand when the arms reach and the neck is held, which is what the plan is for; what is in the way is the arms' work, which the owner's 40 J holds. Nothing is
switched on. The films (`Saved\Films\n6_fwd_hand_today`, `n6_fwd_hand_aim`: the forward fall after a shove by hand) show what both falls share: the shove throws the body nearly level, like a plank, and it lands on its
face; with the reach the hands are on the floor under the chest 0.1 s before the face, and the arms fold under the body.

**A third variant, judged by the same limits: no reach in a fall backward** (`fall.reach_back` 0, added; 1 in the file), with reach 5, yield 1 and the head kept off. It is the closest and still misses three:

| Limit | Result |
| --- | --- |
| F2 knockdowns, rest, energy, floor | 8 and 8; 2.68 s; 28.4 J by blows, 38.3 J by hand; 1.01 cm: **met** |
| F3 C14 hand first | 86% by hand, 100% by blows: **met** |
| F4 C15 lower than today; C16 no higher | by hand **17%** (75 today); by blows **50%, as today, not lower: missed**; C16 4.25 and 1.85 m/s: met |
| F5 the arms under 30 rad/s | **the hand at 34.9 and 36.4 rad/s** as it meets the floor after the sideways blows (0 and 180 degrees): **missed** |
| F6 the stand | **148, 131** against 149, 132: one fall worse in each: **missed** |
| F7 the matrix | 160: met |
| F8 the suite | one fails: `room_movable_fall_gets_up`, as with the head's values alone (its first get-up given up) |

Films for the owner (`Saved\Films`): `n6_fwd_hand_reach_noback` (the shove forward, as above: still a dive, the arms fold, the face reaches the floor), `n6_side_blow_today` and `n6_side_blow_reach_noback` (200 N*s
from the side, from the front: in both the body goes over straight, knees unbent, like a plank; with the reach the near hand meets the floor under the shoulder and the head is turned to the upper shoulder; the
camera loses the body after 4.1 s). **Recommended for the owner's look, not switched on:** these values; what they still need is the wrist's speed at the floor (N8 A's wrist model) and the trunk and legs of step 6.


### The natural body, N4 step 1: mechanics first (the same night; not met, off; for the owner to confirm)

**Note of 26 November 2026:** Put off by the owner on 26 November 2026 until the assist is torques of the legs ("N4 put off and N5 in the session"), which is now part of R4 (docs/active-ragdoll.md); the figures here are of the body before the head.

Built behind `reactions.reflex_delay_s` (0 in the file: the hashes of `hold`). A hit felt while standing starts the clock, unless it runs already; until it ends the flinch (its state, pose offset and
weakening) and a doubling over wait, the balance strategies keep the goals they had (`UpdateStrategies` returns), and no step is decided; a fall by the posture and the knockdown by a hit on the head do not wait.
The clocks of the plan's step and intent (`balance.step_delay_s`, `reactions.intent_delay_s`) are not built: a step is decided as soon as the reflex's delay is over.

Limits written before the runs (R1 to R6), at a delay of 0.08 s (C9's human range is 0.07 to 0.11):

| | Limit | Result |
| --- | --- | --- |
| R1 | the hashes of `hold` with the delay at 0 | 117 the same |
| R2 | the run at 0.08 and the run with everything held back (10 s, standing for "every reaction off") the same until the hit + 0.08 | the first frame that differs is 3.100 s (the flinch at 3.089): **met**, but two runs that both delay share whatever is not delayed, so R3 is the test that counts |
| R3 | `react_unseen_chest_30`: the first change of a muscle's target 0.058 to 0.102 s after the hit; no step before | 0.100 s (today 0.011), the first change of strength 0.089 (today 0); no step: **met** |
| R4 | `react_two_hits`: a second hit 30 ms later does not start the clock again | the flinch at 3.089, as with one hit: **met** |
| R5 | the push matrix at 0.05, 0.08, 0.11, beside the owner's table (for the owner) | **156, 155, 153 of 160** (today 160). Fewer steps at 30 N*s (3 against 6); at 80 and 200 N*s cells that call for a step end "held": the standing assist's pull keeps working while the body waits and takes back what the step would have caught |
| R6 | at 0.08: knockdowns 8 of 8 both ways; the stand no worse than 149, 132; the suite read | knockdowns 8 and 8; **the stand 145, 120**; the suite fails five: `measure_reaction` (it holds N1's known answer, a strength change within 0.012 s, which the delay changes on purpose), `head_slap_cheek_turns`, `prop_fall_on_box`, `strike_kick_calf`, `strike_kick_torso` |

**Not met (R5, R6); nothing is switched on.** What the delay shows is the plan's first risk: while the body waits the assist holds it, so it steps less and a cell that wants a step reads "held"; the plan's fallback is
to apply the delay once N2's step 5 (the assist as the joints' own torques) is done. The largest jump of a command over the suite at 0.08 is 487 N*m (35 scenarios over 200), against today's 429 (39). The film of a hit
of 30 N*s from the side (`Saved\Films\n4_hit30_today`, `n4_hit30_reflex08`, every 33 ms from 3.0 s) shows no difference at that scale: the trunk goes back a few centimetres in both; the figures above are where
it shows. `react_unseen_chest_30` and `react_two_hits` are in the suite and switch the delay on themselves.

**A second attempt, after an independent reading of the code.** The reader found the neutral claim holding and four defects in the delay: the standing assist kept pulling the body back while it waited (the
cells "held"), the waiting flinch added up every crossing of the threshold in a contact (about 60 N*s against 10), a hit on the frame the delay ended started it again, and a fall left the strategies held to the
end of the delay. Made again: the state is a flinch at the hit, so the assist lets go as in any flinch, and only the flinch's pose and weakening, the strategies' new goals and a step wait (`FlinchStartedS` is the
end of the delay); a brace comes no earlier than the reflex; a fall ends the hold. Still neutral (the hashes of `hold`). The same limits:

| | First attempt | Second |
| --- | --- | --- |
| R2, first frame that differs | 3.100 s | 3.100 s |
| R3, first target change; step | 0.100 s; none | 0.100 s; a step lifts at 0.089 (the reflex): the hit of 30 N*s that today's body stands now takes two steps |
| R5, push matrix at 0.05 / 0.08 / 0.11 | 156, 155, 153 | **155, 156, 155** (left: "held" at 80 N*s on a thigh, and blows on the head at 30 N*s that step) |
| R6, knockdowns by a blow; by hand | 8; 8 | **6** (one at rest after 4.1 s); 8 |
| R6, the stand | 145, 120 | 150, **127** |
| R6, the suite | five fail | eleven fail (two of them `react_*`, whose own tuning event changes nothing once the run is tuned already) |

**Not met either way; off.** The second form is the one in the code.


### The natural body, N5 step 1: perception that judges and does nothing (the same night; for the owner to confirm)

**Note of 26 November 2026:** Since 26 November 2026 `Tools\play.ps1 -Perception` tells a session's body what moves and switches on gaze, anticipation and the guard for that session; the files keep them off ("N4 put off and N5 in the session"). An audit of the same day found that anticipation's brace changes no stiffness: every `tone.*` level is 1, so only its pose acts.

The body is told what moves near it (`USandboxBodyController::TellSeen`, a list of `FSandboxSeen`: a name, a kind, a place, a velocity, a size) and judges it in `UpdatePerception`: in view within
`perception.view_half_angle_deg` (100) of where the head faces, noticed after `perception.notice_s` (0.1 s) in view, a threat when something noticed closes on the head or the trunk at
`perception.threat_speed_mps` (1.5) or faster with `perception.threat_time_s` (0.4 s) or less to the contact, or is within `perception.threat_near_cm` (30) of the face. It does nothing with it yet.
A run tells it the hands, the feet and the things with mass (`FScenarioStage::TellBodyWhatMoves`) only when the scenario's `body` says `"perception": true`; a session does not tell it yet.
`control.perception` has the frames told and seen, the things noticed, and every threat with the part, the time to the contact it reckoned, the gap and the closing speed.

Limits written before the runs (P1 to P5):

| | Limit | Result |
| --- | --- | --- |
| P1 | a scenario without `"perception"`: the hashes of `hold` | 117 the same |
| P2 | with `"perception": true` the same hashes as without | `perceive_hand_front` and the same file with `false`: the same: **met** |
| P3 | a hand at the face from the front is a threat before the contact, its time to contact within 0.05 s of the real one; from behind not seen; a hand hovering by the chest no threat; with the view broken on purpose the front hand not seen | front: a threat at 3.522 s by the body's clock (3.533 by the run's, one frame later: the pitfall of the two clocks), the contact at 3.744: before it, but **0.290 s reckoned against 0.211: 0.079 off, missed** (0.068 was first written with the two clocks mixed) (the hand still gathers speed, 2.36 m/s then and 2.96 at the contact, and the reckoning takes the speed as it is); behind: seen in 0 frames, no threat; hovering at 0.3 m/s: noticed, no threat; the view at 0.5 degrees: seen in 6 frames, never noticed, the scenario fails |
| P4 | the same hashes twice | the same |
| P5 | `SandboxBody` includes nothing of `SandboxInteraction` | nothing |

`perceive_hand_front`, `perceive_hand_behind` and `perceive_hand_hover` are in the suite. What would put P3 right is the closing speed's change in the reckoning; not done.

**Step 2, the gaze** (`perception.gaze`, 0 in the file). The body attends to a threat first, else the nearest thing it has noticed that moves (`gaze_move_mps` 0.2), and stays on it for `gaze_dwell_s`
(0.5) before another may take its place. The neck's target turns toward it: the way to it seen from the chest, `gaze_head_share` (0.7) of the angle (the eyes, in M5, the rest), at most
`gaze_max_yaw_deg` 35 and `gaze_max_pitch_deg` 25, no faster than `gaze_speed_dps` 200. The head's assist is not turned with it. Limits written before (G1 to G4):

| | Limit | Result |
| --- | --- | --- |
| G1 | with the gaze off, the hashes of `hold` and of the three `perceive_*` | the same |
| G2 | `gaze_follows_hand` (a hand circling 1 m in front, 50 cm round, 0.5 m/s): mean error under 15 degrees, the neck never at its bound; broken on purpose, above 20 | **10.5 degrees**, 0 frames at the bound; with the head's share at 0: 24.3, and the scenario fails |
| G3 | the neck's target never jumps (under 1 degree beyond its motion) | 0.88 degrees: met, near the limit |
| G4 | no step, no fall | none |

**Met.** The close-up (`Saved\Films\n5_gaze_close\sheet.png`, six moments of the loop) shows the head turning up, aside and down after the hand. What looks wrong: the eyes do not move (the plan's risk of a stare;
the eyes are M5's), the head follows the hand without a pause like a camera on a slow, steady circle, and with the hand low the head bows so far that the face is hidden. `gaze_follows_hand` is in the suite.
`gaze_rests_on_viewer` needs the player's head, which a run does not tell yet.

**After an independent reading of steps 1 and 2.** The neutral claims, the signs and the turn's conversion were found right. Put right, each neutral (the hashes of `hold` and of the three `perceive_*`):
a gaze switched off while the head is turned brings it back at its own pace (it stayed turned for good); the head goes back while the body falls, lies, gets up or is posed from outside; a hand held near the face
is one threat, not one at every sway of the head (no closing speed asked for near the face, and a threat stays one while it still closes in twice the time); a hand on a course that passes the head is no threat
(the nearest the two come, within their sizes); the view is reckoned from the head's middle, not the base of the neck, and straight above the head keeps its yaw; the neck's target comes up to its turning speed
over `perception.gaze_rise_s` (0.1 s) and slows to arrive; the threat looked at is the one nearest its contact. After them: `gaze_follows_hand` 10.5 degrees, the neck's largest jump 0.48 degrees (0.88 before);
with the gaze on and a hand coming at the face, the largest jump of any target is the struck forearm's at the hit (4.5 degrees, the flinch), not the neck's. P3's time to the contact was tried once more, with the closing speed's gain from one frame to the next counted in: the hand that had just started read 0.091 s for 0.222, 0.131 off the other
way (the jolt of a start is no measure of what follows); taken out, P3 stays missed. Left: the neck's turn assumes the neck in its reference
pose (the flinch and the fall poses tilt it by up to 40 degrees); a long thing's size is its bounding sphere (a stick reads as near the face from 85 cm); the threat's time is the body's clock.

**Step 3 in part: the look at a hold.** With the gaze on, the body looks at the place it is held at, after a threat and before anything that moves, whether that place is in view or not (it is felt);
the brace of the held chain and the free hand to the holder are not built. Limits written before (K1 to K3), on `hold_arm_answer` (kept in `Saved\Recon\n5`, not in the suite: the left forearm taken
by a hand with the grab on and pulled 8 cm to the side for 3 s):

| | Limit | Result |
| --- | --- | --- |
| K1 | the gaze off: every hash as before | 117 of `hold`, the three `perceive_*` and `gaze_follows_hand`: the same |
| K2 | a pull of about 150 N: the body leans against it 2 cm or more; the head within 20 degrees of the hold on average | **the pull was 53 N on average (76 at most), a third of the plan's**; the pelvis 2.7 cm and the chest 2.0 cm away from it (the hold's own lean, `reactions.hold_felt`, as without the gaze); **the head 62.6 degrees from the hold: missed**. The forearm hangs at the hip, 99 degrees from straight ahead; the head may turn 35 degrees and bow 25 and takes 0.7 of the angle by design, so the limit as written could not be met there; it was written before the geometry was looked at |
| K3 | no step, no fall; the neck's target does not jump | none, none; the largest jump is the held forearm's at the grab (4.1 degrees, the flinch), not the neck's |

**Step 4: anticipation** (`perception.anticipate`, 0 in the file). A threat the body noticed starts, `perception.react_s` (0.15 s) later and unless the blow has landed already, the brace (the
tone's braced level, the whole body, for `anticipate_hold_s` 1 s) and the flinch's pose (shoulders up, head in), eased in over `anticipate_pose_s` (0.1 s); `control.perception` counts the anticipations,
their lead before the contact and the false alarms (no contact within a second). The guarding arm is step 5, not built. Limits written before (A1 to A5), with the tone's levels proposed for N3 by `-Tune`
(the file's are 1); the scenarios are kept in `Saved\Recon\n5`:

| | Limit | Result |
| --- | --- | --- |
| A1 | anticipation off: every hash as before | 117 of `hold` and the four perception scenarios: the same |
| A2 | a slow punch at the chest (2 m/s from 70 cm) seen from the front against the same from behind: the brace at least 50 ms before the contact; the chest moves 0.7 times as far or less | braced 0.10 s before: met; **the chest 16.6 cm seen against 8.6 behind: missed**. The two are not alike (32.8 against 24.9 N*s, the chest's front against its back); **the same punch from the front with the anticipation off moves the chest 15.6 cm**: the brace changes nothing that shows, as N3 found (the chest's give is the whole body's lean on its feet) |
| A3 | a jab at 5 m/s from 50 cm: no anticipation | none (the blow lands before the reaction is due) |
| A4 | a hand hovering by the chest: no anticipation, no false alarm | none, none |
| A5 | the pose comes in without a jump | not separable: the run's largest jump is a recovery step's (10.8 degrees on the calf, 0.3 s after the hit), the pose itself is eased by its own clock |

**A2 missed; off.** What the plan's figure asks of a brace is more than the tone's levels can give the trunk. At the head it shows (information, not a limit): the same slow punch at the face moves
the head 17.5 cm braced against 25.1 without (0.70), braced 0.11 s before; but the braced body then takes two steps where the other stands (23.5 against 20.6 N*s: the braced neck passes more of the blow on).

**After an independent reading of steps 3 and 4.** The neutral claims held. Put right (neutral, the hashes of `hold` and of the perception scenarios): the brace is a tone of its own beside N3's,
gated off in a fall and on the floor (it had taken the one slot a scenario's `tone` event uses, and held a falling body stiff); the bookkeeping runs after what the body felt in the frame and keeps an
anticipation until it is judged, so that no lead or false alarm is lost; a threat while it holds keeps it holding; with the hit's own flinch on, the anticipation adds only what it has beyond it (the two
were laid on each other, up to twice the flinch's pose); `anticipate_pose_s` must be above 0 while it is on; a hold on the head is not looked at (the neck chased it), and of two holds the stronger is.
The same runs after them: the chest 16.5 cm with the anticipation against 15.6 without (A2 still missed); the face 18.0 against 25.1 (0.72); the fast jab and the hovering hand as before. An ending
of the pose at the hit was tried in between and took the head to 31.8 cm: the pose has to hold through the contact. Left: the legs' weight terms of `assist.internal_share` and
`balance.ankle_as_torque` can still change in one frame as the body leaves upright (both off and not recommended).

**Step 5 in part: the guarding arm** (`perception.guard`, 0 in the file; it rises with the anticipation, so that is on too). A threat to the head with `guard_time_s` (0.3 s) or more to the
contact, or held near the face, raises the arm on its side: its hand goes `guard_reach_cm` (30) from the face toward the threat and follows it, for `guard_hold_s` (1 s), the elbow kept down so that
the forearm stands before the face; the hand is placed as a hand target is but is not one of the placed feet (that list keeps the body from stepping). The free hand to a holder is not built.
Limits written before (U1 to U4), on `seen_fist_raised_guards` (a fist brought slowly to 30 cm before the face and held there a second; kept in `Saved\Recon\n5`):

| | Limit | Result |
| --- | --- | --- |
| U1 | the guard off: every hash as before | 117 of `hold`, the perception scenarios and both `pose_hand_place`: the same (see below for the arm's blend, which first applied to every arm and moved them) |
| U2 | the near hand rises 30 cm or more and comes within 15 cm of the line from the face to the fist; no fall, no flinch | 69 cm, 9.0 cm; standing, no flinch: **met** |
| U3 | a fast jab and the hovering hand raise no guard | none, none: met |
| U4 | the arm's targets do not jump (under 1 degree) | **0.48 degrees, met after three changes**: first 9.2 (a raise in 0.2 s); the arm's weight is now eased a second time on top of the limb's own ease, and the arm is solved for its whole target and blended by its joints' turns, not by where its hand is (blended by the hand's place, the hand went past the shoulder on its way from the hip to the face and the arm folded there); and the raise takes `guard_blend_s` 0.5 s, the anticipation's pose `anticipate_pose_s` 0.2 s (in a raise of 0.2 s the arm's own swing of 110 degrees is about 2 degrees by this measure). At 0.2 s the guard's hand also met the fist and made the body flinch |

The close-ups (`Saved\Films\n5_guard_close` before the elbow was kept down, `n5_guard_close2` after): with the elbow down the forearm stands before the face and the hand is at the fist from 4.3 to
4.6 s; on the way up and down the arm swings out to the side, and before (`n5_guard_close`) the arm pointed at the fist, straight, like a reach. **For the owner to look at.** The arm's blend by its joints' turns (and its weight eased twice) is the guard's alone (`FSandboxLimbTarget::bJointBlend`): made for
every arm at first, it moved both `pose_hand_place` scenarios and the fall's reach (the recommended variant: 44.7 J by hand instead of 38.3, the head struck in 33% of the falls by hand instead
of 17%), so the hand target and the reach keep the blend they were measured with (their hashes and figures as before: 28.4 and 38.3 J again). The anticipation's figures above were taken with
`anticipate_pose_s` 0.1 (0.2 in the file now).

After an independent reading of the guard: put right, each neutral (the hashes of `hold`), a guard and the fall's reach fighting over one hand (a fall leaves a guarding hand to the guard's release for the
whole fall), a guard's side left over from a threat that started no anticipation (it rose seconds later toward the old thing), a guard whose thing was gone on its first frame (the hand was sent to the
world's origin and never let go), and a guard on a body posed from outside. Left: a second guard on the same hand within the first's release starts from where the first was; a threat that passes close
before the face moves the guard's point fast; the guard needs `perception.anticipate` on and says nothing when it is off. The same limits again: 69 cm, 9.0 cm, 0.48 degrees, no guard for the jab and the
hovering hand.

### The natural body, N8 A taken: the head is the default body (26 November, the owner's decision)

**Note of 26 November 2026:** C25's baseline was set to 439 N*m the same day with four of N2's repairs on (docs/natural-body.md, decision 15); the baselines after `head` are `n2`, then `led`.

The owner chose the head (question 1 of the night's report), and, once C25 had been measured (below), to keep it with C25's
baseline at 512 N*m. (Corrected on 26 November 2026: set to 439 N*m the same day; docs/natural-body.md, decision 15.) The plan and limits were written before the runs (`Saved\Recon\head_switch_plan.md`). What changed:

- `Specs/physics/npc_body.json` is the night's head spec, so `PA_SandboxNPC` has the head as a body of its own on the neck; the
  body before it is kept as `npc_body_neck.json`, `PA_SandboxNPCNeck` (the wrist's and the toes' assets are still made from the
  body before). Every scenario and tool names `PA_SandboxNPC` as before.
- `assist.head.angular_nm_per_rad` 0 (was 10: the world's hold on the head) and `muscles.neck.damping_s` 0.05 (0.1).
- What a scenario or a tool did to `neck_01` meaning the head it now does to `head`: `hit_head_30_stands`, `measure_push_neck`,
  the first touch of `measure_drop_head_first`, the bodies of the push matrix, the compliance table and the grab pulls, the stand's
  blows on the head. A pose's `neck_01` stays (its angles are shared out between the two joints). `SANDBOX_HEAD_BODY` has turned
  round: it names the bone that stands for the head in an asset whose head is part of the neck (`neck_01` for `PA_SandboxNPCNeck`,
  `PA_SandboxNPCWrist`, `PA_SandboxNPCToes`), and a scenario's `head` (in `body`, `at_bone`, `bone`, `equals`) becomes that bone.
- The pose database (`Tools\pose-prior.ps1 -Build`) was built again: it refuses one made for another body, and the scorecard
  stopped on it. Its pose figures are not to be compared with `before_track`'s.

**The same body as the night's.** Every one of the 117 scenarios of `hold` ends with the hashes the night's battery on the head
had (`bat_head2`, 117 of 117, the edited ones among them): what the night measured by `-Tune` and a copy of each scenario is what
the files give now.

**The eight scenarios, set anew** (accepted by the owner with the decision; each changed to what the new head does, and why):

| Scenario | Was | Now | Why |
| --- | --- | --- | --- |
| `effector_hit_head` | falls, lying | stands (`fell` false, `standing`) | The fist is felt by the head as 31.0 N*s (by the body before it as 35.3, an edge against the knockdown's 35); the hand delivered 40.0 against 45.5. Ended at x = 3 it is felt as 31.9. A fist at 5.5 m/s (`strike_punch_head`) still fells it: 36.1. The path is as it was |
| `palm_slap_head` | path to x = 5 | path to x = 3 | 4 cm inside the narrower head, as it ended inside the old one: 7.2 N*s, the head turns 10.7 degrees, standing > flinch > standing |
| `measure_drop_head_first` | first touch `neck_01`, `hands_ahead_first` false | `head`, true | The left hand lands 3.6 cm ahead of the chest 0.1 s after the head and before the trunk: by C14's rule a hand ahead first. An edge of 3.6 cm |
| `measure_push_neck` | the push on `neck_01`, 14.8 to 16.4 N/cm, 1.22 to 1.35 cm | on `head`, 10.8 to 12.0 N/cm, 1.66 to 1.84 cm | Two joints of 40 N*m/rad together are softer than one: 11.4 N/cm and 1.75 cm settled; 5% either way as before |
| `strike_punch_shoulder` | 2 hits, joints under 30 rad/s | 1 hit, under 32 | A lucky spot. At its point (140 cm) the collarbone turned at 8.2 rad/s on the body before; at 136, 138, 142 and 144 cm at 26.6, 28.1, 31.0 and 5.6. The head body: 26.3, 27.6, 30.7 (its point), 31.4, 5.4. The head moved the contact by a frame (913 N against 646) and showed what was there (`Saved\Recon\shoulder`). Pre-existing: a punch at 8 m/s on the shoulder turns the collarbone at about 30 rad/s two centimetres off this point |
| `tune_live_file`, `tune_live_override` | joints under 30 rad/s | under 40 | A body with its legs switched off falls on its head, and the light neck is whipped: 37.7 and 35.8 rad/s (24.8 and 25.2 before, then a forearm). The whip is the known cost of the head: 37 to 46 rad/s in falls where the body before had 16 to 36 |
| `room_movable_fall_gets_up` | 20 s | 40 s | The body stands at 31.9 s, at its third attempt (the first two fall back at 15.9 and 23.7 s); the body before stood within the 20 s |

**The battery on the default body** (`Saved\Recon\bat_headdefault`), the limits of the plan beside it:

| | Limit | Result |
| --- | --- | --- |
| Tests, suite | pass | 48 tests; 127 of 127, and 128 with `-Long` (the 10-minute stand) |
| Push matrix | 160 | 160 |
| Knockdowns | 8 and 8 | 8 by a blow (24.4 J at most, rest after 2.18 s), 8 by hand (28.5 J, 1.88 s) |
| Stand, three seeds | 147 of 150, 129 at the first attempt | 150, 134 (46, 45, 43): the night's figures |
| Grab pulls | 93, at most 3 falls | 93, 0 falls |
| Record and replay, the session | pass | `-Twice` 13 of 13, `-Takes` 3 of 3, the self-test and the grab demo pass |

The stands that written claims rest on, with the body before it (`PA_SandboxNPCNeck` with its old values, the same code) run the
same hour:

| Stand | Head | Before |
| --- | --- | --- |
| Movable furniture room, seeds 1 to 3 | 49, 50, 49 of 50 (48, 46, 38 at the first attempt) | claimed 50, 49, 50 |
| Pressed with 300 N | 142 of 150 stand; 39 stood while pressed (11, 14, 14) | 144; 50 (12, 23, 15) |
| Held by one hand | 138 of 150 (the tool's 45 a seed missed by seed 2: 44); 46 pulled free and stood | 143; 46 |
| Held by two hands | 141 of 150; none pulled free | 139 (seed 2: 44, missed); none |

Pressed, one in four gets up instead of one in three (39 against 50; the seeds go from 11 to 23, so the difference is about the
spread). Held, both bodies are on the tool's edge of 45 a seed, each missing it once.

**The scorecard** (`Tools\natural.ps1 -Quick`, `Saved\Natural\head_default`): C14 by hand 83% of 6 (86% of 7 before the track),
by blows 100% of 5 (80%); **C15, the head strikes the floor, by blows 63% of 8 against 50%, C16 the head's speed there 3.68 m/s
against 2.82: the light head on a soft neck meets the floor harder after a blow**; C17 within (2.18 s, 24.4 J, 0.79 cm); the
others as before. **C25, missed against decision 15:** the largest jump of a command over the suite is 512 N*m (`calf_r`,
`grab_pull_standing` at 5.10 s: a foot's target turns 37 degrees in a frame at a lift, case a of decision 13) against 429 on the
body before (`foot_l`, the same scenario), run the same hour; 38 scenarios over 200 N*m against 41. With the jumps' repairs "all
but reach" by `-Tune` the head's largest is 440 (`getup_left`, `hit_chest_200_side_falls`) and 10 scenarios are over 200; with them
`effector_hit_head` falls again. N8 A's own limits had no C25; it was found here, after the owner had chosen, and the owner kept the
head with C25's baseline at 512 (`docs/natural-body.md`, decision 15). (Corrected on 26 November 2026: set to 439 N*m the same day; docs/natural-body.md, decision 15.)

Baseline `head` (127 scenarios) from here on.

### The natural body, N2: four of the jumps' repairs on (26 November, the owner's decision)

**Note of 26 November 2026:** The owner set C25's baseline to 439 N*m (docs/natural-body.md, decision 15); baseline `n2`.

The owner chose to switch the repairs "all but reach" on, with the whole battery first and any miss told before a commit
(question 2). Limits were written before the runs (`Saved\Recon\n2_switch_plan.md`), on the head as the default body.

**All five ("all but reach") missed two limits** (`Saved\Recon\bat_n2on`): `effector_hit_head` fell (the fist's 31 N*s staggered
the body for 2.4 s instead of 1.2, and as it brought its feet together they slid apart and it went to its knees: `Saved\Films\
n2_fist_head_on` against `n2_fist_head_off`), and the matrix's cell of 30 N*s on the head at 270 degrees took two steps where one is
allowed (the cell that stands on the 4 cm of a step). Everything else held: knockdowns 8 and 8, stand 150 (130 at the first attempt),
grab pulls 93 with one fall, movable room 50, 49, 50, C25 440 N*m with 10 scenarios over 200 (from 38).

**Each switched off alone** (`Saved\Recon\n2_ablate`): the fall goes with `settle_smooth` or `land_ease_s` off; the cell's two steps
with `lift_smooth`, `settle_smooth` or `land_ease_s` off; `release_smooth` and `fall_ease_s` touch neither. It is the three together.
Both variants then had the whole battery (`Saved\Recon\bat_n2_no_settle`, `bat_n2_no_land`):

| | Without `settle_smooth` | Without `land_ease_s` |
| --- | --- | --- |
| Suite | `palm_close_on_chest` (below) | `grab_pull_standing` falls |
| Matrix, knockdowns | 160; 8 and 8 | 160; 8 and 8 |
| Stand, three seeds | 150, 130 at the first attempt (seed 3 again: a job of the queue gave no result) | 150, 130 |
| Grab pulls | 93, 1 fall | 93, 1 fall |
| C25 | 439 N*m (`calf_l`, `hit_chest_200_side_falls`); 13 over 200 | 450; 34 over 200 |

**Taken, by the owner's choice: without `settle_smooth`** (`lift_smooth` 1, `release_smooth` 1, `land_ease_s` 0.05,
`fall_ease_s` 0.1). **`palm_close_on_chest` was set anew:** its palm pushes the chest at 3.35 s and the body steps back out of the
hand from 3.56 s, in every variant; the grip closed at 3.9 s and met the body only where it had swayed back in time (at 3.83 s with
all five, at 4.0 without `settle_smooth`, so on air). The grip now closes while the palm still presses (3.35 to 3.45 s), and passes
on all three bodies (the files, all five, none). The pitfall was written down before ("Close a grip while the hand still presses").

The fall on bringing the feet together is not repaired, only not met: it is the family of the night's hand on the chest (on the body
before the head), and `settle_smooth` stays off until it is understood. The films of the hand on the chest on the head
(`n2_hold_chest_head*`, from the side and the front) show no difference that the eye can name between the repairs and none: in both
a long lunge back, unnatural, and the feet together by 8.5 s.

### The natural body, N6, the reach on today's body (26 November; kept off by the owner's decision)

The night's third variant (`fall.aim` 1, `arm_reach_stiffness` 5, `arm_reach_damping` 2, `arm_yield_stiffness` 1,
`arm_yield_damping` 2, `reach_back` 0, and the head kept off: the falling neck at 120 N*m/rad and the fall poses' neck angles) by
`-Tune` on the head with four of N2's repairs (`Saved\Recon\bat_n6now`), against the same body without it (baseline and report `n2`):

| | Without the reach | With it |
| --- | --- | --- |
| C15 the head strikes, by hand / by blows | 55% / 63% | **18% / 25%** |
| C16 the head's speed at the floor, by hand / by blows | 4.16 / 3.00 m/s | 4.28 / **1.20** m/s |
| C14 a hand first | 83% / 100% | 83% / 100% |
| Knockdowns by blows / by hand | 8 / 8 | **6 / 5**: rest after up to 4.18 s (four falls over 3 s), and the shoves at 0 and 180 degrees no longer fell the body |
| Most energy gained, blows / by hand | 14.2 / 32.1 J | 38.0 / 38.7 J |
| The hand at the floor | under 30 rad/s | 35.3 rad/s (`hand_l`, the blow at 45 degrees) |
| Stand, three seeds | 150, 130 | 148, 133 |
| Matrix, suite | 160; 127 | 160; `hit_chest_200_side_falls` comes to rest too late |

The head is kept off the floor far better, and the falls do not come to rest in time. The owner kept `fall.aim` off (question 4):
the next work of N6 is why a fall with the reach comes to rest so late and why two shoves by hand no longer fell the body.

### The natural body, N4 put off and N5 in the session (26 November, the owner's decisions)

**N4** (question 5): the reflex's delay stays off until N2's step 5 (the assist as torques of the legs); its figures were the
night's, on the body before the head, and were not measured again.

**N5 in the session** (question 6): `Tools\play.ps1 -Perception` tells the body every frame what the hands, the foot and the
things do (`FScenarioStage::TellBodyWhatMoves`, as a scenario's run with `"perception": true`) and switches on gaze, anticipation
and the guard for that session (`-Tune` after it still chooses); the tuning files keep them off. A recording of such a session is
replayed with `"perception": true` and the same switches by console at 0 s (`Tools\record-replay.ps1 -Perception` runs it,
recording `demo_seeing`). Checks: the suite with the hashes of `n2`; the default session's replay to the same hashes
(`540f5e9f`); the takes; the self-test with and without `-Perception` (14 of 14); the grab demo.

The seeing session's replay first ended 2.1 cm off at the pelvis and 6.5% off in a hand's impulse, a flinch a frame apart. Ruled
out one by one: the hands seen before the replay's recording begins (not telling them changed no hash), and the switches coming by a
console change at 0 s (given from the first step instead, no hash moved). Each behaviour on alone found it: with nothing on and with
anticipation alone the replay held (0.008 and 0.009 cm), with gaze alone it drifted the whole 2.1 cm. What the body was told showed
why: in the session the hands fly from where the pawn makes them to where the recording begins in the 0.7 s before it does, and the
gaze followed that flight; the replay has them there at once. A session that records now shows the body its hands only while a
recording runs: the replay holds, 13 of 13, the pelvis 0.008 cm, the frames of the flinches the same. In the demo the hands come at the
chest with 0.11 s to the contact, under the anticipation's 0.15 s, so it neither braced nor raised a guard; that is for the owner's
look in the headset.

### The owner's look in the headset with perception (26 November)

`Tools\play.ps1 -VR -Perception -Record vr_seeing`: 311 s, seven takes, 89.9 frames a second, both controllers tracked throughout.
By the session's log the body took 94 things for threats (77 a hand near the face), braced before 43 of them and raised a guard 41
times (34 left, 7 right; a fist held on each side of the face in a scenario raises the arm of that side, so the count is where the
hands were, the body having turned in its get-ups), was hit 61 times (44 on the head) and fell 6 times (five by the pelvis's tilt, one
knockdown of 37.2 N*s on the head). The usual course: a hand coming at the face at 0.7 to 0.9 m/s is a threat 0.34 to 0.44 s before
the contact, the brace and the forearm come 0.16 s later, and the contact is soft (about 5 N*s). The recordings do not replay to the
session (the pelvis 67 to 144 cm off by their ends; grabs and hard pushes, as in `vr_check3`), so nothing more is read from them.

**The owner:** the head turned after the hands. The bracing was not noticed. **The get-ups still look unnatural:** a get-up is
several clips with clear pauses between them, the body does not begin at once but after a delay, and it is all mechanical, robotic:
"far from natural behaviour and movement yet".

What the log shows of the get-ups (`Saved\Recon\getup_pauses.py`): the body lay still for 1.5 to 2.0 s before each began (6 s once,
held), `getup.rest_s` 1.5 s for every fall alike; each took 4.0 to 6.2 s, and in each the clip stood still, waiting for the body, for
0.8 to 1.3 s in all: the pauses between the clips' phases. From the fall to standing 6 to 8 s, where a person needs 2.6 to 4.2 s from
the first move (C21). This is N7's matter (a get-up that never holds a pose).

### The natural body, N7 first: the get-up's pauses (26 November, the owner's decision to take it now; not met, off)

**Note of 26 November 2026:** The get-up measured here is the clip-following one. Since 26 November 2026 the get-up is led (`getup.carry` 1, "R1: the led get-up"): 3.2 s, paused only in the 0.61 s ease, the plank 0.10 to 0.20 s while rising, the clip no longer laid over the loaded feet.

Built, all 0 in the files (the suite with the hashes of `n2`): `getup.slow_from` and `getup.rate_follow_s` (the clip slows as the
body falls behind, to a stop at the lags that stopped it, runs while the body eases in, its speed smoothed), `getup.rest_per_ns_s`
and `getup.rest_max_s` (the body lies `rest_s` plus that per N*s of the hit that felled it). Measures, in `control.get_up`:
`paused_s` (the clip under a tenth of its rate), split into `paused_ease_s`, `paused_behind_s`, `paused_end_s`; the causes of
waiting, `behind_height_s`, `behind_side_s`, `behind_foot_s`, `behind_held_s`; `lay_s`. Limits before the runs:
`Saved\Recon\n7_getup_plan.md` (L2 the stand 147 and 129, L3 the pause at most half, L4 the get-up no longer).

**Where today's pauses are** (150 get-ups of the stand): 2.42 s of a 5.0 s get-up in the median; 0.60 s while the body eases in
(`ease_s`), 1.81 s waiting for the body mid-clip, 0.01 s at the end. The waiting is for a foot in the air more than 15 cm from the
clip's (0.96 s median) and the pelvis more than 20 cm aside (0.73 s), rarely its height (0.16 s). Lying before it: 1.82 s.

| Variant (by `-Tune`) | Stand, first attempt | Pause | Get-up | Lying | Suite |
| --- | --- | --- | --- | --- | --- |
| Today | 150, 130 | 2.42 s | 5.01 s | 1.82 s | 127 |
| V1 the clip slows (`slow_from` 0.5, `rate_follow_s` 0.15) | **147, 126** | 1.72 | **6.55** | 1.82 | 127 |
| V2 V1, and the rest by the fall (0.4 s + 0.004 s/N*s, at most 2) | **143, 125** | 1.69 | **6.51** | 1.29 | `prop_getup_on_plate` |
| W1 a foot waited for at 25 cm, 0.35 s | 150, **106** | 1.52 | 4.11 | 1.81 | 2 get-ups fail |
| W2 the pelvis aside at 30 cm | 150, **123** | 2.13 | 4.72 | 1.81 | `getup_held` |
| W3 W1 and W2 | **148, 105** | 1.26 | 3.84 | 1.81 | 3 fail |
| W4 W3 and `ease_s` 0.3 | **146, 103** | **0.93** | **3.52** | 1.80 | 4 fail |
| X1 the get-up's muscles follow the clip's speed (`pose_velocity_share` 1) | 149, **120** | 2.24 | 4.83 | 1.76 | 127 |
| X2 X1 and W4 | 147, **85** | 0.90 | 3.49 | 1.72 | 4 fail |
| R1 the rest by the fall alone | 149, **126** | 2.40 | 4.99 | **1.24** | 127 |

**Not met; everything off.** Slowing the clip lengthens the get-up; letting it wait less shortens it into a person's 2.6 to 4.2 s
and halves the pause (W4), but a fifth of the attempts then fail, and a failed attempt and a second one read worse than a pause: the
film of `getup_back` with W4 (`Saved\Films\n7_getup_back_w4`) ends crouched on the floor, where today's
(`n7_getup_back_today`) lies still from 3.5 to 6.0 s, turns onto its side with an arm held straight up, crouches and stands by
10.5 s. The muscles following the clip's speed (X1) changes nearly nothing: it is not damping that holds the body back. The pauses are
the body not keeping up with the recorded clip, its feet and its pelvis; no value of the get-up as it is removes them without
losing get-ups. Shortening the rest alone (R1) takes 0.6 s off the lying and misses the first attempts by three.

**Why the feet lag** (the owner's question). In the waits for a foot the pelvis is in its place (2 cm aside, 1 cm in height) and the
foot is off 18 cm as the wait begins and as it ends, half a second later, when `foot_wait_s` cuts it: the foot does not come (26 of
the waits at one moment, stand_B at 0.72 s). The pelvis aside comes closer in some waits and hovers at its 20 cm in others. The feet's
wait (and their assist) aims at the clip's own foot, the sample's skeleton scaled by the pelvis's height, not at where the body's legs
put the foot. Three repairs, each behind a switch at 0 (the suite with the hashes of `n2`), each judged by the same limits
(`Saved\Recon\n7_getup_plan.md`):

| Variant | Stand, first attempt | Pause | Get-up | Suite |
| --- | --- | --- | --- | --- |
| P1 a foot is waited for only while it comes 1 cm closer every 0.15 s (`getup.progress_cm`) | 149, **114** | 1.79 s | 4.38 s | 2 get-ups fail |
| P2 P1 and the pelvis too (`progress_pelvis`) | 148, **91** | 1.30 | 3.89 | 3 |
| P3 P2 and `ease_s` 0.4 | 147, **91** | 1.11 | 3.70 | 1 |
| S1 the get-up's legs 1200 N*m/rad (cap 350), spine 900 (400) | 149, **118** | 2.17 | 4.76 | 127 |
| S2 legs 1600 (450), spine 1200 (500) | 150, **114** | 2.37 | 4.96 | 127 |
| F1 the feet aimed at the foot of the pose asked of the muscles, chained on the mesh (`getup.reached_feet`, `FSandboxClipPose::ReachedBodyAt`) | 148, **101** | 2.23 | 4.82 | 3 |
| F2 F1 and S1 | 147, **104** | **1.27** | **3.86** | 2 |

What each showed. Not waiting for a foot that does not come lost first attempts: while the clip waits, the rest of the body
settles, and that is what the wait buys. Stronger muscles let the pelvis keep up (its waits fell from 202 s to 54 over the stand) and
the feet then waited longer. `ReachedBodyAt` was checked on `getup_back` before it was used: its foot is 5 to 10 cm from the clip's
throughout, mostly the two skeletons' proportions; the 18 cm that a hip held 25 degrees short of the clip was predicted to give is not
there (the prediction was wrong, and the code's comments say so). Aimed there, the feet wait less and the body fails more.

**Over thirteen variants one result: every change that makes the pauses shorter makes fewer get-ups succeed at the first attempt**
(85 to 126 of 150, against 130). A recorded clip with an assist gets the body up because it waits for it; the pauses are the price of
that, and no value of the get-up as it is takes them away. Everything stays off; the switches and the measures stay for the next step.

**A wait that is not a freeze** (the plan's item 7; `getup.gather_s`, `gather_rate`, 0 in the file): while the clip waits, its time
goes back by up to `gather_s` and comes again at half its rate, never past where the wait began. The freezes in mid-clip go (0.00 s
in the median; only the 0.6 s of easing in is left), and so do get-ups: G1 (0.15 s) 142 of 150 stand, 102 at the first attempt, five
get-up scenarios fail; G2 (0.3 s) 140 and 105, six. Even a target that moves back a little keeps the body from catching up: it needs a
still one. With S1's stronger muscles too (GS) it is worse: 134 of 150, 80 at the first attempt, nine scenarios. Sixteen variants in
all, one trade between the pauses and the get-ups that succeed. Videos for the owner, the get-up from
the front in real time: `Saved\Films\n7_front_today\film.mp4` and `n7_front_g1\film.mp4` (G1 gets up at its first attempt there).

### N7: the plank, measured; unload and then step (26 November, the owner's choice; not met, taken out)

**Note of 26 November 2026:** The get-up measured here is the clip-following one. Since 26 November 2026 the get-up is led (`getup.carry` 1, "R1: the led get-up"): 3.2 s, paused only in the 0.61 s ease, the plank 0.10 to 0.20 s while rising, the clip no longer laid over the loaded feet.

**The owner, on the videos:** no visible difference between the get-up with pauses and without; the get-up is as unnatural as
before, the body rises "like a plank, like a rocket on its launcher". So the pauses are not what the eye sees.

**The plank, measured** (read only, kept: `control.get_up.plank_s`, `plank_max_cm`, `rise_feet_cm`, `rise_trunk_deg`, the last
attempt). `plank_s` is the time after easing in with the feet carrying 0.8 of the weight or more and the centre of mass more than
5 cm outside them (the rule of 25 November); `rise_*` the farther foot from the pelvis over the floor and the trunk (pelvis to chest)
from vertical when the pelvis first comes 70 cm above the floor. Checked before use on `getup_back`: `plank_s` 1.700 s against 1.709
from the same rule over `series.csv` (a frame apart), furthest out 45.2 cm against 45.5; the rise, 35.8 cm and 52.9 degrees, lies
between the trace's checkpoints at 9.0 s (34.7 cm, 51.8) and 9.5 s (51.5, 45.0). The suite with the measures has the hashes of `n2`.
In that fall: at 9.0 s a half-kneel, the pelvis 53 cm up, the left foot 34 cm to the side of the pelvis and not ahead of it, the
right foot 34 cm behind; at 9.5 s the pelvis is 80 cm up (standing: 90) with both feet behind it along the lean (27 and 48 cm) and
the trunk 50 degrees forward; the feet carry the weight with the centre of mass 21 to 33 cm outside them until 10.5 s.

**Today on the stand of 150** (median, p90): plank 1.36 s (1.88), furthest out 46 cm (62); at 70 cm the farther foot 37 cm (47),
the trunk 70 degrees (81). In the log's samples (every 0.25 s) 927 are a plank: 633 of them with both feet within 20 cm of the
clip's, 294 with a foot further. At clip times 1.6 to 2.0 s, the pelvis 70 to 74 cm up, in all four clips. So the plank is mostly
not a foot left behind on its own; the clip is laid over the loaded feet (`frame_follow_s`), and the lean is of the body as a whole,
as 25 November found ("the body is pitched forward as one piece").

**Unload and then step** (the plan and its limits before the runs: `Saved\Recon\n7_unload_plan.md`): a loaded foot more than
`step_lag_cm` over the floor from the clip's owes a step; the clip waits, the pelvis's assist takes the centre of mass over the other
foot (at most 25 cm), and once the foot carries 0.15 of the weight or less it is carried to the clip's foot, raised 8 cm; let go after
1.2 s. Limits: plank median at most half of today's and the farther foot at 70 cm at most 30 cm (U1), the stand 147 and 129 (U2),
the suite (U3), the get-up at most 0.8 s longer (U4), the assist at most 0.03 more (U5).

| Variant | Stand, first attempt | Plank, s | Foot at 70 cm | Get-up | Steps owed, let go | Suite |
| --- | --- | --- | --- | --- | --- | --- |
| Today | 150, 130 | 1.36 | 37 cm | 5.01 s | - | 127 |
| U-a `step_lag_cm` 20 | **148, 107** | **1.36** | **33** | **6.52** | 524, 198 | 127 |
| U-b 15 | **132, 63** | **1.13** | **33** | **6.64** | 3162, 143 | 5 get-ups fail |
| U-c U-a, unloaded at 0.25 | **148, 107** | **1.23** | **33** | **6.54** | 539, 186 | 127 |

**Not met by any; taken out** (`Saved\Recon\n7_unload_step_full.patch`; the measures stay). A foot often cannot be unloaded: the
pelvis's 90 N takes the centre of mass three quarters of the way to the other foot and the foot keeps 160 to 220 N (`getup_back`,
U-a: the step let go after 1.2 s). While the clip waits, the body leans back over its feet by itself in about a second; that is a
pause, not a step. **This was the third attempt of 25 November again** ("the foot held until it is free": lifted, the clip waiting,
the pelvis aimed toward the other foot; 1.45 s off the feet to 1.05, 99 first attempts of 150). I told the owner that a shift of the
weight had not been tried; it had, and the section above says so. Today adds the measure and the 68% of plank moments with the feet
where the clip has them.

What the measurements of both days say together: the recorded get-ups close the gap between the centre of mass and the feet by moving
the feet fast (18 to 55 cm in 0.6 s of clip, 25 November) while the body is ahead of them, a motion carried by its speed. This body
follows slower, through waits, and the lean that the recorded person passes through in a moment is held by the world for 1.4 s.

### N7: a get-up made for this body, step 1, the rise from a crouch (26 November, the owner's choice; stopped before the stand)

**Note of 26 November 2026:** Superseded by "R1: the led get-up" (26 November 2026). `getup.rise`, `getup.fast_rise` and `getup.end_one_foot` stay at 0 and are removed at R2's step 0 (the owner's decision of 26 November); "The recording, carried" below was R1's first form.

Built behind `getup.rise` (0 in the file; the suite with the hashes of `n2`, 48 unit tests): from the back the body follows
`M_ragdoll_getup_crouch_B` (a get-up into a crouch) only to its crouch (`rise_at_back_s` 0.95), and rises from there by itself:
the muscles go over from the clip's crouch to the stance over `rise_s` (the clip's layer capped, `ClipHold.ShareCap`), the pelvis's
assist goes up to the standing height (measured at start, `StandPelvisHeightCm`) and upright, aimed over the floor to take the centre
of mass over the middle of the feet; the rise waits while the pelvis lags or the centre of mass is more than `rise_com_cm` outside the
feet; a lean of the body's own (`rise_lean_s`, `rise_lean_max_deg`) fades by its own clock after the rise. Plan and limits before the
runs: `Saved\Recon\n7_rise_plan.md`. **No limit was judged**: four tries on `getup_back`, each a diagnostic written down before it ran,
and none worth the stand.

- The body reaches the clip's crouch (pelvis 53 cm up, both feet loaded) with its centre of mass **22 cm ahead of the feet**. The
  recorded person is rolling forward there; held still, it is a fall the world holds.
- Waiting for the centre of mass: it never comes (the pelvis's 90 N does not move it); given up after 3 s (`Saved\Films\n7_rise_try1`).
- Rising without that wait: up in 2.0 s, the centre of mass 13 to 36 cm ahead the whole way, `plank_s` 2.5 s against 1.7 for that fall
  today (`n7_back_rise_side\film.mp4`, today's `n7_back_today_side\film.mp4`, the same camera).
- Leaning by the ankles (as the ankle strategy does standing): the centre of mass came from 23 to 18 cm, then the body tipped over its
  toes (`n7_rise_lean1`). A centre of mass ahead of the toes is beyond what a foot can push back.
- Leaning at the hips: the body stays crouched with half its weight on its hands, 31 cm ahead; given up after 3 s.

What it shows: the crouch of a recorded get-up is no place to stand still on this body either. Every pose this body reaches on its
way up by a recorded clip has its centre of mass ahead of its feet; a slow get-up has to get its feet under it first, with the hands
carrying the front of the body while they come.

**The owner on the side videos: both still rise like a plank at the end.** The plank at the end is the world's: the pelvis's assist
turns the straight body upright about its feet while the centre of mass is ahead of the toes. A person there catches the lean with a
step. Tried (`Saved\Recon\n7_handover_plan.md`, limits before the runs): the get-up ends as soon as the pelvis is within 15 cm of its
standing height and tilted less than 25 or 32 degrees, with both feet on the floor, and the standing balance catches the rest. New
measure, kept: `plank_after_s`, the plank's rule for 3 s after a get-up ends standing (0.389 s on `getup_back` by the controller and by
`series.csv`).

| Variant | Stand, first attempt | Plank while getting up + after | Get-up | Suite |
| --- | --- | --- | --- | --- |
| Today | 150, 130 | 1.36 + 0.24 = 1.61 s | 5.01 s | 127 |
| Hand over at 25 degrees | **148, 113** | **1.17 + 0.42 = 1.59** | 4.19 | **3 get-ups fail** |
| Hand over at 32 degrees | 149, **124** | **0.98 + 0.56 = 1.60** | 3.94 | 127 |

**Not met; taken out** (`Saved\Recon\n7_hand_over.patch`). The get-up is a second shorter and the plank is not: most of it comes while
the body is tilted more than 25 to 32 degrees, before a hand-over is possible, and what is handed over is held by the stagger's upright
hold (150 N*m on the pelvis) instead of the get-up's assist. Of the 150 handed over at 25 degrees, the first-attempt ones fell by 17:
a step does not catch a body that far ahead of its feet.

**The rise as fast as the recording** (the owner's choice; `getup.fast_rise`, 0 in the file; limits before the runs:
`Saved\Recon\n7_fast_plan.md`). From the clip time at which the clip's hands leave the floor for good (the last time either is lower
than `fast_hand_cm` 9 above its floor; stand_B 1.30 s) the clip does not wait for the pelvis or a foot, and the pelvis's assist may
pull with up to `fast_force_n` and turn with up to `fast_torque_nm`, eased in over `fast_ease_s`; both the controls and the assist's
measured load take those caps (`ApplyFastRiseCaps`). The suite with the switch at 0 has the hashes of `n2`.

| Variant | Stand, first attempt | Plank while getting up + after | Get-up | Assist | Suite |
| --- | --- | --- | --- | --- | --- |
| Today | 150, 130 | 1.36 + 0.24 = 1.61 s | 5.01 s | 0.28 | 127 |
| F-a, 400 N and 500 N*m in the rise | 150, 135 | **0.87 + 0.17 = 1.04** | 4.01 | 0.33 | 127 |
| F-b, no more pull, only no waits | **148, 74** | **1.41** | 3.86 | 0.27 | **4 fail** |

F-a misses the point's limit (half of today's plank, 0.81 s) and meets the others: more first attempts, a second shorter, the assist
within the 0.35 allowed. The plank is a third shorter. Without the stronger pull (F-b) the body falls behind its clip and is lost.
Side videos of whole get-ups for the owner: `Saved\Films\n7_whole_back_today`, `n7_whole_back_fast`, `n7_whole_front_today`,
`n7_whole_front_fast`. The switch stays at 0 until the owner has looked.

**The owner on those videos: nothing is better.** The last part of the get-up is still wooden, a rocket raised on its launcher, and
the get-up still reads as separate animations glued together with pauses between them. The switch stays at 0.

**The recording, carried** (the owner's choice, against the track's principle 1 for the get-up; `Saved\Recon\n7_carried_plan.md`). Values
only, by `-Tune`: no waits (`lag_cm`, `side_lag_cm`, `foot_lag_cm` 500), the clip not laid over the loaded feet (`frame_follow_s` 0), the
pelvis's assist 6000, 8000 N/m and 2000 N*m/rad capped at 600 N and 600 N*m, the feet's 4000 N/m across and 1000 up capped at 300 N, and
in C-a the legs 2000 N*m/rad (cap 600) and the spine 1500 (600). One switch was needed and is in the code at 0: `getup.end_one_foot`
(carried hard, the pelvis was held at the clip's height and one leg did not reach the floor, 0 N on it, so the get-up never ended).

| Variant | Stand, first attempt | Pause | Get-up | Plank while + after | Assist | Suite |
| --- | --- | --- | --- | --- | --- | --- |
| Today | 150, 130 | 2.42 s | 5.01 s | 1.36 + 0.24 = 1.61 s | 0.28 | 127 |
| C-a, carried, muscles too | 149, 140 | 0.61 | 3.20 | **1.37 + 0.48 = 1.97** | 1.17 | **`grab_getup_held`** |
| C-b, carried by the world alone | 148, 134 | 0.61 | 3.20 | **1.86** | 1.03 | **`grab_getup_held`** |

Every get-up now takes the clip's own time and pauses only while the muscles come up. By the measure the world holds the body longer,
not shorter: with one leg short of the floor the centre of mass is outside the one foot that carries it. One hand no longer keeps hold
of a body getting up (the grip held 376 frames of the 500 the scenario asks). Videos: `Saved\Films\n7_whole_back_carried`,
`n7_whole_front_carried`.

### R1: the led get-up (26 November, the second architecture, `docs/active-ragdoll.md`; behind `getup.carry` at 0, for the owner's look)

**Note of 26 November 2026:** On since the owner's decision of 26 November 2026 (the last paragraph of this section): `getup.carry` 1, baseline `led`, the suite 126 of 127 with `grab_getup_held` failing until R2 repairs the hold by one hand.

The owner asked for the body to be rebuilt as a hybrid active ragdoll; the audit, the causes and the plan are in
`docs/active-ragdoll.md`. Its first part is the "recording, carried" above done for the whole body: `FSandboxCarry`, a spring from
the world on each of the 19 bodies toward where the clip has it (`FSandboxClipPose::ReachedBodyAt`, the pose asked of the muscles
chained on this body's proportions), stiffness and caps per kg of the body, the reference's velocity given with the target; the
clip runs at its rate and waits only for a press that holds the body down; the clip is not laid over the feet; the old assist on
pelvis and feet is off; a hit of `carry_yield_ns` (60) or more lets the body go at once. The controls are made when a led get-up
first begins. With the switch at 0 the suite has the hashes of `n2` (127), and the 48 unit tests pass.

**What the world was doing, read from a log line added for it** (the force on each body, four times a second). The first build
gave 1.57 of the body's weight on the stand, and most of it pressed the body **down**: 700 to 980 N of a weight of 784 N, with the
trunk's bodies 4 to 10 cm above their targets while lying and sitting. The reference lies lower than the floor lets this body lie
(another skeleton, the pelvis's height scaled). So: the world never pushes a body down; a body above its reference is given the
reference at its own height.

| Variant (`-Tune "getup.carry=1"` and) | Stand of 150, first attempt | Assist, mean | Suite |
| --- | --- | --- | --- |
| Today (`getup.carry` 0) | 150, 130 | 0.28 | 127 |
| First build: pushes down too, 2 weights each way | 148, 127 | 1.57 | - |
| No push down, the world lifts at most 0.5 of each body's weight | **140, 70** | 1.23 | 2 fail |
| No push down, at most 1 weight | 148, **107** | 1.19 | - |
| **No push down, at most 2 weights (the values in the file)** | 149, **139** | 1.16 | 126 (`grab_getup_held`) |

Capping what the world may lift costs get-ups and saves no assist: the legs do not keep up with the recording on their own, as in
every variant of this day, and a failed attempt costs a second one. What is left of the 1.16 is mostly sideways: a thigh pulled
with 200 to 300 N toward a reference whose foot is 20 cm from where this body's loaded foot stands. Laying the clip over the
loaded feet again (`frame_follow_s` in the led get-up) gave 0.97 against 1.05 on `getup_back` and a body that leant longer after it
stood (0.73 s against 0.13); not kept.

**The four get-ups, led** (`Saved\Suites\r1_led`): each at the first attempt in 3.20 s (today 5.0 s in the median, 2.4 s of it
paused), paused 0.61 s (the ease). As the pelvis passes 70 cm the trunk is 22 to 30 degrees from upright and the farther foot 20 to
26 cm from the pelvis (today's medians: 70 degrees, 37 cm). The plank's measure reads 0.10 to 0.20 s while getting up and 0.22 to
0.58 s after (today 1.36 + 0.24 in the median of 150). The sheet `Saved\Films\r1_back_led_sheet` shows the body sit up, roll over a
foot into a crouch and rise with its feet under it.

**FR-18 on the led get-up.** A push of 30 N*s does not stop it. A blow of 200 N*s lets it go ("hit with 200 N*s while it was
led"), it falls and gets up at the second attempt. Pressed with 1500 N it is held down, gives up after 3 s and gets up afterwards.
A press and a hold first did nothing to it: two pressing hands did not slow it and one holding hand was torn off, the world getting
the body up against them. So a press or a hold takes the pull away in proportion (nothing left at `getup.hold_force_n`, back by
the release's clock). With that, on the stand of 50 (seed 1): pressed with 300 N, 15 of 50 stand under it (today 39 of 150); held by
two hands, 3 of 49 pull free (today none); **held by one hand, 42 of 50 pull free** (today 47 of 146) (Corrected on 26 November 2026: 46 of 146 with `hold_felt` on; 47 was the figure before it.), and `grab_getup_held` fails
(the grip does not last its 500 frames). One hand no longer slows a led get-up: open, the next thing to repair.

**A mistake of mine on the way.** A helper written in the shell to add one tuning name to five files replaced every tab of two
source files with a letter and doubled the line breaks of three (a nested array of one pair is flattened in PowerShell, and
`String.Replace` then got two characters). Both files were taken back from git and the edits made again by hand; nothing else was
lost. `CLAUDE.md` already warns of the array; the lesson added there is to make such edits with the editor, not a script.

Videos for the owner, the same camera, real time: `Saved\Films\r1_back_today`, `r1_back_led`, `r1_front_today`, `r1_front_led`.

**The owner on the videos: the led get-ups look natural, the ones before do not; switch it on.** `getup.carry` is 1 in the file
since 26 November. Before that, one more try at the hold by one hand: with the caps of the world's pull taken down with a press as
well as the springs, two hands hold every body (0 of 49 pull free) and one hand still does not (34 of 50), but only 2 of 50 stand
under a press of 300 N (15 without it, 13 on the body before); not kept, the caps stay whole. Switched on: 48 unit tests; the suite
126 of 127 (`grab_getup_held`: the grip of one scripted hand does not last its 500 frames on a body that gets up in 3.2 s; left
failing, not loosened, the first thing of R2); against `n2` 112 scenarios with the same hashes and 15 changed, each one with a
get-up (the `getup_*`, `effector_shove_knockdown_gets_up` and the others that get up): shorter rest after the fall (8.2 to 9.7 s
against 10 to 11), the run's mean assist share 0.16 to 0.29 against 0.06 to 0.12, and `getup_hit_strong` now falls to its blow and
gets up at the second attempt. New baseline `led`. The stand 149 and 139; the session's self-test 14 of 14, its grab demo and the
record and replay hold. Not run: the bootstrap, the session with a person, the headset.

### Checks after the audit of the documents (26 November)

No code, value or scenario changed; read from the engine's source and the project's, and two tools timed.

- **The non-linear joint solver is switched per joint, not by a console variable.** In 5.8 the world solver reads
  `FConstraintProfileProperties::bUseLinearJointSolver` of each constraint (default true; `ConstraintInstance.h`, "World Solver:
  Whether to use linear solver for this joint"), set on a physics asset's constraint templates and applied by
  `UpdateConstraintFlags_AssumesLocked`. Chaos keeps two containers, `LinearConstraints` and `NonLinearConstraints`, and a joint
  whose flag changes is destroyed and made again (`FPBDRigidsEvolutionGBF::SetJointConstraintSettings`: it loses its state). No
  console variable switches the world solver; `p.Chaos.Solver.Joint.UseLinearSolver` does not exist (it would be accepted in
  silence as a dummy). `p.Chaos.ImmPhys.Joint.UseLinearSolver` and the physics asset's `SolverSettings.bUseLinearJointSolver` are
  for immediate-mode physics (the Rigid Body anim node), which the project does not use. Nothing in the project sets the flag, so
  every joint, Physics Control's among them (`RigidBodyWithControl.cpp` sets it true), is on the linear solver.
- **`states.<state>.effectors...` is accepted and read by nobody, as `Config/Tuning/README.md` says.** The store takes
  `states.<state>.<name>` for any declared state and declared number (`FSandboxParamStore::CheckNumberKey`); the hands' and tools'
  values are read with no state (`FSandboxInteractionTuning::FromParams`) and their range check runs on those base values only, so
  such a value is not checked against its range either. The body reads only its own names.
- **Times, on the reference machine:** `Tools\getup-seeds.ps1` with three processes a stand (nine in all) 92 s, 149 of 150 standing,
  139 at the first attempt, assist share 1.16, the figures of R1 (`CLAUDE.md` had 110 s, of a run not recorded here) (Corrected
  in the review of the audit, 26 November: it is recorded, "106 to 114 s for the three", where the stand was made shorter);
  `Tools\natural.ps1 -Quick` 56 s.
- **`strike_punch_belly_steps` falls by a threshold, not by losing its feet.** The fist at 6 m/s, 0.1 s in the belly: the body
  feels 79 N*s on `spine_04` (78.6 into the belly), flinches at 3.10 s, doubles over at 3.14, needs a step at 3.16 (capture point
  16.6 cm out), steps back 43 cm with the left foot (3.19 to 3.33), and is Falling at 3.41 because "pelvis tilted by 36.9 degrees"
  (`thresholds.fall_tilt_deg` 35): the doubled-over pose tilts the pelvis itself. Lying at 4.47. The same run with
  `-Tune "thresholds.fall_tilt_deg=45"` does not fall: doubled over, a step, Standing at 4.23, a step that brings the feet
  together, Standing at 5.56 (four steps in all; hashes `a95dcc09` against `bb7cb4a7`). Videos from the side, real time, 3.0 to
  7.5 s: `Saved\Films\belly_punch_side` and `belly_punch_side_tilt45`. The name says "steps" since the scenario was made (111 N*s,
  four steps, standing); it has fallen since the owner's decision of 25 November for `effectors.hand.strike_through_cm` 40, and its
  expectations were rewritten then. (Renamed `strike_punch_belly_falls` in the same commit, decision 11 of
  `docs/active-ragdoll.md`; noted in the review of the audit.)
- **What the world gives a led get-up, on the stand of 150** (the same run as the times above, `Saved\Matrix\getup_seeds\timing_1126`):
  the mean share of each fall's get-up, of the body's weight, 1.16 on average, 1.07 in the median, 0.92 at the 10th percentile, 1.63 at
  the 90th, 2.20 at most; the peak within an attempt 2.25 in the median, 3.01 at the 90th percentile, 3.35 at most. R1's "peaks of 2.5"
  was not the stand's.

### The review of the audit (26 November)

No code, value or scenario changed. Read from the saved runs and the code; two scenarios made for the review were run from files
outside the repository (a fall at 3 s, the hands sent at 6 s by `at_bone`, grab on as in the files) and filmed.

- **C27 on the stand is not the four `getup_*`.** Every run of the stand records `control.get_up.rise_trunk_deg` and
  `rise_feet_cm`. On the led stand of 150 (`Saved\Matrix\getup_seeds\timing_1126`) the trunk is 60.1 degrees from upright in the
  median and the farther foot 24.9 cm from the pelvis; both of C27's limits (45 degrees, 30 cm) are met in 65 of 149 get-ups. The
  "22 to 30 degrees and 20 to 26 cm" of R1 are the four `getup_*`, which use only the clips from the back and the front; the side
  clips give 61 to 63 degrees in the median. Like for like the led get-up took the stand from 70 to 60 degrees and from 37 to 25 cm.
- **C19 on the stand:** 18 of 169 attempts given up, 10.7% (the "about 12%" was 18 per 150 falls); the longest pose asked for
  (`target_still_longest_s`) is up to 2.98 s, longer than 1.0 s in 5 of 150 falls.
- **The figures "before R1" of the hold and the press are of older bodies.** 47 of 146 pulled free of one hand with
  `reactions.hold_felt` off and 46 with it on, both before N2's repairs; 39 of 150 stood under 300 N on the head body before them (13
  of 50 is 39 shared by three, not a run of seed 1). On the body R1 replaced (`n2on_hold1`, `n2on_press300`): one hand, 16, 18 and
  15 pulled free, 49 of 147; pressed with 300 N, 14, 15 and 14 stood, 43 of 150. R1's conclusions stand.
- **What was run after the R1 commit and not written down.** The bootstrap at 17:01 (`Saved\Tools\bootstrap.json`): `ok` false, it
  stopped at step 7, the suite, on `grab_getup_held`, which fails on purpose until R2; it cannot pass until then. By hand after it:
  the push matrix at 17:03 (160 runs, ok, 45.3 s), the knockdowns at 17:04 (8 of 8, 16.5 s), the stand of seed 1 at 17:04 (50 of
  50, 40.8 s) and the session's self-test at 17:06 (`room_stand_npc`, ok). The record and replay and the grab demo were not run.
  `Saved\Baselines\led\push-matrix.json` is `n2`'s file (same size, 09:44): `Tools\baseline.ps1 -CompareMatrix led` compares with
  `n2`'s matrix.
- **`drop_npc` on the body of 19:** a foot 8.28 cm into the floor and 129.6 J on the way out (`Baselines\led`); the 149 J of the
  READMEs was the body of 18.
- **Two hands take both ankles of a body getting up and pull it along the floor** (`Saved\Films\review_ankles_two`, film and
  video from 5.5 s). The body falls back at 3 s and begins to get up at 5.59 s (clip `M_ragdoll_getup_stand_R`); the hands close on
  `foot_l` and `foot_r` at 6.32 s and their targets go 2 m away over 4 s. The hands are left behind, at their cap, and their grips
  slip after 0.74 and 1.51 s (45 cm for 0.25 s). While they hold, the world gives 700 to 1180 N in all (0.9 to 1.5 of the body's
  weight), its net pull against the hands; the sustained press the body feels comes to 295 N of the 450 that would take the pull
  away. The clip runs on, reaches its end at 2.60 s of it, and the body stands there on one foot, held by the world at about its
  own weight (the right thigh at its cap, 240 N), for 3 s; the attempt is given up ("waiting for the body", which at the clip's end
  means not upright on both feet), it falls at 11.78 s and stands at the second attempt (17.84 s). Hashes `60cb2f19` headless and
  rendered.
- **The same by both wrists** (`Saved\Films\review_arms_getup`), the targets 150 cm toward the head and 60 cm up: grips slip after
  1.1 and 1.9 s; the world gives up to 2180 N (2.8 weights), the trunk held 50 cm off toward the clip against the hands; the arms
  are pulled straight by the hands and do not reach toward the pull; then the same stand at the clip's end, given up, a fall at 11.81
  s, standing at the second attempt. Hashes `5999e2e5`.
- **A hit during a led get-up.** `getup_hit_strong`: the blow of 200 N*s at 8.50 s, while getting up, fells the body at once
  (GetUp, then Falling in the same frame), but the scenario does not ask for it (it asks for two hits, a get-up and standing at the
  end). `getup_push_weak`: 30 N*s at 8.50 s, the body stands at 8.79 s. Below `getup.carry_yield_ns` (60) a hit does not touch the
  world's pull; only a sustained press does.
- **The tabs of the plan** were exported and held against the files word by word: "Active ragdoll 2" and "Natural body" differ only
  as intended; "Tab 1" differs only by its three drawings, whose labels are the Mermaid's.
- **After the owner's decision 23** (a scenario may name a `known_failure`, read by `Tools\suite.ps1`; `grab_getup_held` carries it
  until R2): build OK, 48 unit tests, the suite 126 of 127 with `grab_getup_held` counted apart and exit 0 (47.7 s); against `led`
  126 scenarios with the same hashes and none changed, and `strike_punch_belly_falls` with the hashes `led` has under its old name
  (`bb7cb4a7`, `6cc6e5d7`); `grab_getup_held`'s hashes as in `led`. The bootstrap then ran all 14 steps, every one passing, in 281 s
  (the suite 46 s, the push matrix 47 s, the knockdowns 18 s, the stand 43 s, the record and replay 33 s, its three takes 49 s,
  the grab demo 10 s, the session's self-test 10 s).

### The player's movement in the headset (FR-42, decision 24; 26 November, decisions 25 to 28)

The player's side only (`SandboxInteraction`; the session's demo in `SandboxScenario`); no value of the character changed. Built
without the headset: arithmetic by unit tests, the player's body by scenarios, the way through the pawn by a session without a
person in a made-up headset. Not yet tried in the headset.

**The patch of the capsule** (`docs/recon/m4_part5_body.patch`) applied to the tree of 26 November as it was (`git apply`), and
built on:

- **Its masks did not do what they were for.** In Chaos 5.8 a body's `FMaskFilter` filters scene queries only
  (`FQueryFilterData::NarrowFilter`); the simulation's pairs read channels and block masks (`FShapeUnionFilterData::BroadFilter`).
  The capsule has an object type of its own, `PlayerBody` (`ECC_GameTraceChannel1` in `Config/DefaultEngine.ini`, blocked by every
  profile by default), and the hands, the open hand's plate and the feet ignore it. `body_hands_pass`: a hand (opening on its way
  back) and a foot go through the capsule at 2 m/s, 0 N*s between them and anything.
- **The first way of ignoring it changed two scenarios.** Set in the effector's constructor, the response dropped the collision
  profile the constructor had named (the engine applies a named profile only in `PostInitProperties`, and a response set before
  that clears it): `grab_drag_fallen_two` (a hand's impulse 114.5 to 723.6 N*s) and `grab_two_hands_one_arm` took other hashes,
  again on a second run; with the response removed and the channel kept in the ini, both had `led`'s hashes again. Set after
  `Super::PostInitProperties()`, every scenario keeps its hashes.
- **The capsule was a ball.** The component is registered, and its body made, before it has its tuning; the patch changed the
  shape afterwards. The body is now made again from the capsule's shape in `Start` (the effector of kind `body` only), and the
  capsule's own shape is made once (the engine asks for it again and again). `body_pushes_low_box`: a box of 10 kg, 40 by 40 by
  20 cm, lying on the floor, is pushed 166 to 170 cm by the capsule (a ball at the capsule's middle, 58 to 102 cm above the floor,
  would have passed over it).
- **Pressed at rest against the table it slept a substep in six**: its contact force read 300, 300 and 150 N in turn at no speed.
  It never sleeps now; then 300 N in every frame (`body_walks_into_table`: it stops at the table's edge, 0 cm in it, pressing with
  its cap).

**The body's mass (decision 28).** `body_walks_into_npc`: the capsule walks into the standing character from 1.5 m, its target
eased in over 0.2 s as the stick's is, to 1.4 m/s, and held 17 cm into it for 3 s. Contact at 1.39 to 1.43 m/s.

| Mass | First hit (chest) | Belly over the press | What the character did |
| --- | --- | --- | --- |
| 70 kg (the patch; `damping_s` 0.2, near critical) | 26.0 N*s | 42.6 N*s | flinch, doubled over (the rule at 40 N*s), stagger, three steps back, standing |
| 25 kg | 19.0 N*s | 29.6 N*s | flinch, stagger, steps back, standing |
| 10 kg (decision 28; `damping_s` 0.08, near critical) | 6.2 N*s | 25.5 N*s | flinch, stagger, steps back, standing; 143 N*s in all over the press |

With the patch's first damping (0.08 on 70 kg, a damping ratio of 0.37) and a path that left at full speed, the capsule fell 23 cm
behind its target (300 N moves 70 kg at 4.3 m/s^2 at most) and caught up at 2.4 m/s: 40.5 N*s into the belly. At 0.5 m/s
(`body_walks_into_npc_slow`) 70 kg did not double the character over (7.9 N*s on the chest; it stepped back). At 10 kg the slow
walk is 5.3 N*s on the chest, then stagger and steps, standing. Filmed (`Saved\Films\body_walks_into_npc`): the character takes the
touch, steps back with its arms forward, catches itself and stands.

**The arithmetic** (`FSandboxLocomotion`; `DictatorSandbox.Interaction.Locomotion.StickTurnAndBody` and
`...Source.TrackedHandsRideThePlaySpace`, 50 unit tests in all): the dead zone and the tilt past it; full speed after 0.2 s, 14.78 cm
gone on the way, and rest after another 0.2 s; forward and right taken from where the head looks, also straight down; a snap of 30
degrees about a head 50 cm off the play space's middle leaves the head where it was to 1e-6 cm, one turn a tilt, the next after the
stick came back under 0.3; the body stays while the head leans 15 cm and follows at 2 m/s beyond 20 cm; a push halfway between 100
and 250 N takes half the walk into it, past 250 N all of it, and none of a walk along it; standing still, a pressed body's target is
put where the body is. The tracked hands get the play space's velocity (without it 0) and a snap's jump of 15.5 cm, less than a
hand moves in a frame, is caught up with only because it is told as a jump. Broken on purpose (the snap's sign, the play space's
velocity left out): two tests failed, each on its line.

**The session without a person in a made-up headset** (`Tools\play.ps1 -Demo -Walk`; `FSandboxStandInHeadset`: the head 40 cm ahead
and 30 to the left of its play space's middle, turned 20 degrees and 5 down, the controllers beside the hips, grips closed): 8 of 8
checks.

| Check | Figure |
| --- | --- |
| The head stands where the session put it, facing the character | 0.00 cm, 0.00 degrees |
| The stick into the character is held back | in 199 frames, all of it at most |
| The body pushes no harder than a hand | touching in 121 of 160 frames of the press, 72 N on average (428 N in one frame at most); the character steps back and the player follows |
| The view is not darkened by walking into it | 0% |
| Forward and right along the character slides | 64.1 cm to the right in 1 s |
| Snap turns about the head | 30.00 and -30.00 degrees, the head 0.000 cm from where it was |
| The hands are not flung by a turn | 1.68 m/s at most (a blow begins at 2) |
| The hands keep up with a walk | 1.26 cm behind their targets at most |

Broken on purpose (the push's sign turned): the head walked into the character and the view went black (100%), and the run failed.
The session walked 4.38 m.

**Its record and replay** (`Tools\record-replay.ps1 -Walk -Twice`): the recording has three effectors (`hand_left`, `hand_right`,
`body`); the replay's body first touches `spine_02` at 1.42 m/s as the session's did, the right hand `hand_l` at 0.62 m/s, and two
replays have the same hashes (`a7928716`). After three seconds of shoving the replay ends 5.5 cm from the session's pelvis, with one
flinch fewer, and the hands' and the body's impulses 10 to 11% apart (the left hand touched nothing in the replay): a session with a
shove is not replayed to the end. Those rows are shown and not judged.

**Hashes.** `Tools\suite.ps1 -Baseline led`: 126 scenarios with the same hashes, none changed, `strike_punch_belly_falls` with
`led`'s hashes of its old name (`bb7cb4a7`), five new (`body_*`, all passing); 131 of 132 with `grab_getup_held` counted apart.
The desktop session's self-test (14 of 14), the grab demo, `record-replay.ps1 -Twice` (13 of 13) and `-Takes` (3 of 3) pass as
before: the desktop is untouched (decision 27). Saved as the baseline `walk` (132 scenarios). The bootstrap, with the walk as its
15th step, passed every step in 327 s: the suite 50 s, the push matrix 50 s (160 runs), the knockdowns 18 s (8 of 8), the stand of
seed 1 45 s (50 of 50, 48 at the first attempt), the session's self-test 10 s, the record and replay 34 s, its three takes 50 s,
the grab demo 10 s, the walk with its replay 37 s.

### R2, step 0: the clean-up (26 November, decisions 9 and 29)

Every dead switch of R2's list went, code and values, and nothing a run does moved: the suite kept all 132 hashes of `walk`, and
the push matrix, the knockdowns and four stands of 150 falls came out the same trial for trial. 61 lines added, 820 taken out.

**What went.** N7's get-up: `getup.rise` with `rise_at_back_s`, `rise_s`, `rise_com_cm`, `rise_lean_s`, `rise_lean_max_deg` and
`rise_clip_back` (and with them `UpdateRise`, the rise's lean in the pose's turns, the clip's share cap, the pelvis's standing height,
and the two metrics that measured only that rise, `control.get_up.rise_began_s` and `rise_wait_s`; no script read them);
`fast_rise` with its four values (`ApplyFastRiseCaps`); `slow_from`, `rate_follow_s`; `progress_cm`, `progress_window_s`,
`progress_pelvis`; `reached_feet` (`FSandboxClipPose::ReachedBodyAt` stays: the carry aims with it); `gather_s`, `gather_rate`;
`end_one_foot`. N2's variants not taken: `balance.reach_smooth` (and the anim instance's `SetReachWithWeight`),
`stagger_brake_follow_s`, `lift_frame_s`. N3's daze, `tone.daze_*` (and the controller's count of dazes, which nothing asked
for). The three overrides of `assist.head.angular_nm_per_rad` (falling, getting up, lying) that repeated the base 0. The wrist's
asset (`Specs/physics/npc_body_wrist.json`, the generated `PA_SandboxNPCWrist`) and, by decision 29, the spec field `limits_in`
that only it used: the generator's second joint for the swings, its frame `Plain` (folded into `Child`, which it equalled for every
other joint), `FSandboxPoseRig`'s `bInChild` and its unit test `DictatorSandbox.Body.Pose.LimitsInTheChildsFrame`. A spec that still
names `limits_in` is refused rather than built another way.

**What stayed as a rule.** `balance.swing_foot_n_per_m` was not a switch: it was the swinging foot's assist spring, set in place
of the feet's 1500 N/m while a foot swings, and 0. Deleting the code would have handed the swinging foot the feet's springs and
moved every scenario with a step. Its name and value went; the code keeps the rule as it was: the world gives a swinging foot no
pull, only the feet's angular spring holds its turn.

**What stays on purpose:** the clip-following get-up (carry 0, and the fallback when a carry cannot be made) with every value it
reads, until R2 is accepted; `settle_smooth` (off until it is understood); `rest_per_ns_s` and `rest_max_s` (R2's "how long it
lies"); `fall.aim` (until R3); the tone's levels, the joints' scales, `weight_share`, `ankle_as_torque`, `reflex_delay_s` (R4);
the perception's switches (the session's `-Perception`).

**Checks.** The build, no warnings (35 s); 49 unit tests; `Tools\gen-assets.ps1` remade the three physics assets with the changed
generator, and the suite ran on them: `Tools\suite.ps1 -Baseline walk`, 132 scenarios with the same hashes, 131 of 132 with
`grab_getup_held` counted apart. `Tools\push-matrix.ps1` and `Tools\baseline.ps1 -CompareMatrix walk`: 160 runs end as they did.
`Tools\knockdowns.ps1`: 8 of 8, every field of every fall as before but the physics step's time. The stand on three seeds
(`Tools\getup-seeds.ps1`), each trial's hash and time to stand held against a run of the code before the change
(`Saved\Matrix\getup_seeds`: `timing_1126` for the plain stand, `r2s0_before_*` made for this): 150 of 150 the same in each of
the four. The bootstrap passed all 15 steps in 328 s (its stand of seed 1: 50 of 50, 48 at the first attempt).

**The figures before R2's hold, on three seeds** (the held and pressed stands had been run on seed 1 only):

| Stand | Seed 1 | Seed 2 | Seed 3 | Sum | Assist's mean share (seeds) | R2's criterion |
| --- | --- | --- | --- | --- | --- | --- |
| Nobody holds: end standing, at the first attempt | 50, 48 | 49, 44 | 50, 47 | 149, 139 | 1.18, 1.18, 1.12 (1.16) | C20: 95% and 135 |
| One hand (`-HoldHands 1`): pulled free and stood within the hold | 42 | 39 | 38 | **119 of 150** | 1.06, 1.09, 1.04 | 20 to 60 |
| Two hands (`-HoldHands 2`): pulled free and stood | 3 | 7 | 4 | **14 of 150** | 0.74, 0.82, 0.78 | at most 5 |
| Pressed with 300 N (`-PressN 300`): stood under the press | 15 | 18 | 17 | 50 of 150 | 0.99, 1.01, 1.00 | kept near this |

Two hands do not meet decision 13 either: seed 1's 3 of 49 hid it (seed 2 gives 7). With one hand 147 of 150 stand in the end,
with two 144, pressed 145. The authority body by body and by direction (decision 15) is not measured yet: `control.authority` does
not exist; proposed to be built with decision 17 in R2's next step, both of them measures that keep every hash.

### R2, step 2: a get-up that was not led is said, and authority measured (26 November, decisions 15 and 17; the owner away)

Measures only: every hash of `walk` kept. The plan: `docs/recon/r2_step2_plan.md`.

**Decision 17.** `control.get_up` counts the attempts that asked for the led get-up (`getup.carry` 1) as `led_attempts` and
`not_led_attempts`, with the reason of the last one not led in `not_led`; a run with such an attempt fails on a check of its own,
`get_up_led`, whatever it expects, and logs an error. `FSandboxCarry::Create` also refuses a mesh with no simulated bodies (it made
an empty carry before). A session logs an error for each such attempt (`play.ps1` then ends with `PLAY ERROR`) and puts a line
`GET-UP NOT LED ...` on the desktop's HUD while that character lives; the headset's HUD draws nothing (decision 17).
**Broken on purpose** (a build in which `Create` refused every time): `getup_back` was `SCENARIO FAIL` on `get_up_led` (value 1),
`not_led` "the carry's springs could not be made: BROKEN ON PURPOSE ...", its get-up the clip-following one (assist 0.27 of the
weight instead of 1.12, standing all the same: the run would have passed before); a session on a scene whose active body is
dropped from 140 cm logged at 3.34 s "Play session: the character's get-up asked for the led get-up (getup.carry 1) and was not led
(1 time(s)) ..." and `play.ps1` ended with `PLAY ERROR`. Put back: `getup_back` has its hashes again (`bf3da48f`). The HUD's line was
not seen in a picture (a self-test's shot is taken without the HUD); it is the log's text.

**The overlay.** An assist is drawn as active only when its springs, as handed to the solver in that state, are above 0
(`GetControlView`): led, the get-up's assist no longer shows; the carry's bodies are drawn instead (violet: a cross where the world
pulls each centre of mass, a line of 0.05 cm a newton for the force), and the state layer's corner gives the authority now (up, over
the floor, down, in weights; and `GET-UP NOT LED` in red when it is so).

**`control.authority`** (decision 15): every force the world gives the body in a step, the old assist's and the carry's, entry by
entry from the same spring law as the assist's load (`FSandboxAuthority`, filled in `UpdateAssistLoad`; `FSandboxCarry::TorqueOn`
new), added up by state for every settled frame, and body by body while getting up (`FSandboxAuthorityTally`, unit-tested:
`DictatorSandbox.Scenario.Authority.TalliesByStateBodyAndDirection`; 50 unit tests). A state's `mean` and `peak` are of the sum of
the forces' sizes over the weight; `up_mean`, `horizontal_mean`, `down_mean` of their upward, horizontal and downward parts; and
torques. On `getup_back` the get-up's mean is 1.1206, the same number as `control.get_up.assist_force_share_mean`, as it must be.

**Checks.** Build without warnings; 50 unit tests; the suite against `walk`: 132 of 132 with the same hashes (131 pass,
`grab_getup_held` known), 49.1 s, the slowest mean physics step 1.03 ms; `play.ps1 -Smoke` passes. The stand on three seeds
(`Saved\Matrix\getup_seeds\r2s2_plain`): 149 of 150 stand, 139 at the first attempt, as at step 0, and every trial of the 150 has
the hash and the time to stand of `timing_1126`.

**What the world gives a led get-up, by direction and by body** (the stand of 150, the mean of the falls' means, of the body's
weight):

| Seed | Mean | Up | Over the floor | Down | Peak, median fall | Given most |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 1.18 | 0.46 | 0.92 | 0.12 | 2.22 | `spine_02` 0.16, `spine_04` 0.16, `thigh_r` 0.15, `thigh_l` 0.15, `pelvis` 0.11 |
| 2 | 1.18 | 0.45 | 0.93 | 0.12 | 2.29 | `spine_02` 0.16, `thigh_r` 0.16, `spine_04` 0.15, `thigh_l` 0.15, `pelvis` 0.11 |
| 3 | 1.12 | 0.45 | 0.87 | 0.12 | 2.25 | `thigh_r` 0.15, `spine_02` 0.15, `spine_04` 0.14, `thigh_l` 0.14, `pelvis` 0.10 |

Read: (1) **most of the authority goes over the floor, not up**: 0.91 of the weight horizontally against 0.45 upward (the sizes add
to 1.16). The world drags the trunk and the thighs to where the clip has them more than it lifts them: what R2's fit to the floor
and the matcher are to take off (R1 saw it on one fall: "a thigh pulled with 200 to 300 N toward a reference whose foot is 20 cm
from where this body's loaded foot stands"). (2) **The world pushes the body down**, 0.12 of its weight on average and up to 1.27
weights in a frame on `getup_back` (0.10 to 0.17 on the four `getup_*`): "the world never pushes a body down" (`FSandboxCarry`) holds
for the springs' place, a body above its reference being given the reference at its own height, but not for their damping, which
brakes a body that rises faster than its reference. Not changed here (measures only): for the owner, with R2's fit to the floor or
R7, whether the damping's downward part is to be taken off too (it would change every led get-up). (3) The five bodies of the trunk
and thighs take 0.72 of the 1.16; the arms and the head little.

### R2, step 3: the hold by one hand (26 November, decisions 13 and 14; the owner away; behind `getup.hold_by_body`, 0)

The plan, with the screen that changed it: `docs/recon/r2_step3_plan.md`. Behind a switch that is 0 in the files, as R1 was: with
it at 0 the suite keeps all 132 hashes of `walk` and every stand its trials.

**What it does** (`getup.hold_by_body` 1, while led). A hold (a hand's grip, told by `TellHolds` with its pull) is no longer part
of the press: (1) its chain loses the world's pull at once: the held body, the bodies between it and the pelvis, the pelvis, and
everything that hangs from the held body (`hold_chain` 3; a hand on the chest takes the trunk, the neck, the head and the arms), all
of it from a pull of `hold_chain_full_n` (150 N) on (`hold_chain_share` 0); (2) every body loses the pull in proportion to the holds'
pulls added, all of it at `hold_rest_full_n` (450 N, as a press does at `hold_force_n`); (3) the clip's pace goes down in
proportion to the force of the press and the holds added as vectors (`hold_sum` 0), from none at 0 N (`hold_slow_from_n` 0) to a
stop at `hold_force_n` (450 N): one hand's 300 N leaves a third; the press counts at 0.15 of its force in that (`hold_press_pace`),
and a press of 450 N holds the body down as before. Shares and the pace go down at once and come back by `carry_release_s`.
`FSandboxCarry` got each body's parent (from the skeleton), a share for each body in `Apply` (1 for all without a hold: every
product as it was) and `ChainHolds` (unit-tested, `DictatorSandbox.Body.Carry.ChainHoldsReachThePelvis`; 51 unit tests).
`control.get_up` gained `held_s`, `hold_max_n`, `pace_min`, `first_started_s`.

**The screen** (seed 1, the 26 falls on the bare floor, `Saved\Matrix\screen`), in the plan's file: the rule as decision 14 wrote
it (the chain to the pelvis unled, the clip slowed) held nothing: 24 of 26 bodies pulled free of one hand (23 with the switch at
0). The legs, and above the hand the neck, the head and the arms, were still led and drew the body out of the hand. The subtree
(13 of 26) and the rest taken by the force (6 to 10 of 26) did. And a press that slows the clip as a hold does left no body
standing under 300 N (0 of 26 against 10): counted at 0.3 of its force, 44 of 150; at 0.15, 52.

**The stand on three seeds** (`-Tune "getup.hold_by_body=1"`, the values of the files; `Saved\Matrix\getup_seeds\r2s3_end_*`;
the plain stand `r2s3_K1_plain`, run with the press counted at 0.3, which nothing in it reads: no press or hold while it gets up):

| Stand | Switch 0 (step 0) | Switch 1 | Decision 13 / the owner's message |
| --- | --- | --- | --- |
| Nobody holds: stand, at the first attempt | 149, 139 | 149, 139 (every trial's hash the same) | C20: 95% and 135 |
| One hand: pulled free and stood within the 15 s | 119 of 150 (42, 39, 38) | **32** (10, 13, 9) | 20 to 60 |
| ... a grip came off by itself | | 94 of 147 held | |
| ... stand in the end | 147 | 144 | |
| Two hands: pulled free and stood | 14 of 150 (3, 7, 4) | **2** (0, 2, 0) | at most 5 |
| ... stand in the end | 144 | 147; 146 after the review's fix (below) | |
| Pressed with 300 N: stood under it | 50 of 150 (15, 18, 17) | **52** (12, 19, 21) | near 50 |
| ... stand in the end | 145 | 147 | |
| Authority while getting up, mean of body weight: nobody, one hand, two, pressed | 1.16, 1.06, 0.78, 1.00 | 1.16, 0.78, 0.44, 0.99 | not above 1.16 |

**On three other seeds** (4, 5 and 6, so that the figures are not three seeds' luck; `Saved\Matrix\getup_seeds\r2s3_seeds456_*` and
`r2s0_seeds456_*`): one hand, 32 of 150 pull free with the switch (10, 12, 10) against 104 without; two hands, 1 (0, 1, 0) against 11;
pressed with 300 N, 51 stand under it (16, 19, 16) against 49. Standing in the end: one hand 142 against 145, two hands 142 against 146,
pressed 145 against 148 (a body held to the end of the hold has 25 s left to stand in; a few do not). Over six seeds: one hand 64 of 300,
two hands 3 of 300, pressed 103 of 300 (99 without the switch).

**How much longer a held body takes to stand** (from its first attempt to its first standing, the median; three seeds): nobody
holds, 3.2 s. One hand, switch 0: the 121 that stood within the hold took 3.2 s, as if nobody held them. Switch 1: the 34 that
pulled free took 5.2 s (2.0 s more), the other 113 stood once the hand let go, 14.1 s after their first attempt. Two hands, switch
1: 4 stood within the hold (7.6 s), 145 after it (15.2 s). Pressed with 300 N, those that stood under it took 3.5 s (3.2 s at switch
0): the press slows them a little now.

**`grab_getup_held`** with the switch on passes: its grip lasts (the run 2 attempts: the first is given up as the body, held, falls
40 cm behind its clip; it stands at the second once the hand lets go). Its `known_failure` stays while the switch is 0 in the files;
it comes off in the change that puts the switch at 1 (the suite then 132 of 132, measured: `Saved\Suites\r2s3_on`).

**The suite with the switch on** (`Tools\suite.ps1 -Tune "getup.hold_by_body=1" -Label r2s3_on`, compared with `walk` by hand: the
suite refuses a baseline with `-Tune`): 132 of 132 pass; 4 have other hashes, each with a hold or a press while led:
`grab_getup_held` (above); `getup_held` (two scripted hands press the body with up to 244 N while it gets up: the clip slows to 0.92,
standing 3.26 s against 3.20); `getup_pressed_one_hand` (300 N: the pace 0.9, the same attempts and outcome); `room_movable_fall_gets_up`
(a movable piece of furniture presses on the body with up to 163 N while it gets up: pace 0.95, standing 3.23 s against 3.20).
`getup_pressed` and `getup_pressed_two_hands` keep their hashes: their presses (1500 and 600 N) hold the body down as before (a first
build let the pace's share of 0.15 decide that too, and 1500 N no longer held the body: put right before these figures; the stands
above were run again on the build that has it, figure for figure the same).

**Videos for the owner** (one camera, real time, the switch at 0 on the left and at 1 on the right; the stand's trial 17 of seed 1, a
blow of 256 N*s, the hands closing on `spine_04` at 6.9 s and letting go at 20.5 s): `Saved\Films\r2s3_hold1_pair\pair.mp4` (one
hand: on the left the body gets up as if nothing held it, the hand torn off and left hanging at its side, standing at 12.2 s; on the
right it comes onto its hands and knees under the hand, sinks back onto its side, tries again, and gets up once the hand has let go,
at 22.7 s) and `Saved\Films\r2s3_hold2_pair\pair.mp4` (two hands: on the left it rises onto its hands and knees, both grips torn off,
and stands at 18.9 s; on the right it draws its knees up under the hands and stays on its back, standing at 24.3 s, after them).
Each side's film is in `<tag>_a` and `<tag>_b`; `contact.png` beside each pair is eight of its frames. Closer, from the night's
scenario of a hand as the body sits up (`getup_phase_sit_hold`, 2 s from 6.5 s): `Saved\Films\r2s3_sit_hold_pair` (on the left the
hand is torn off as the body sits up and it stands at once; on the right it is held on its back, the free arm reaching up, and gets up
once the hand lets go). Made by `Tools\film-pair.ps1`,
new: the same scenario filmed twice with the same camera and times, once as the files have it (or `-TuneA`) and once with `-TuneB`,
joined side by side with each side's label (the "one command to make the pair" of `docs/active-ragdoll.md`, section 9); the scenario
runs to its end whatever its `end_when_standing_s` says. Not looked at by me as natural or not: that is the owner's eye.

**Decision 7's tables with the switch on:** the push matrix 160 of 160 ending as in `walk`, the knockdowns 8 of 8 (neither gets up,
so neither can move; run all the same).

**One hand on other parts of the body** (decision 31's question c, answered by `hold_chain` 3 for every part alike; seed 1, the stand
of 50, `Tools\getup-stand.ps1 -HoldHands 1 -HoldBone <part>`, `Saved\Matrix\hold_bones`): pulled free and stood within the hold, the
switch at 0 against 1.

| Hand on | Switch 0 | Switch 1 | From the first attempt to standing, median (0, 1) |
| --- | --- | --- | --- |
| The chest (`spine_04`, the stand's own) | 42 | 10 | 3.2 s, 13.5 s |
| The pelvis | 24 | **1** | 10.2 s, 14.0 s |
| The head | 39 | 18 | 3.2 s, 13.7 s |
| The right forearm | 38 | 26 | 3.2 s, 7.5 s |
| The left thigh | 23 | 15 | 12.4 s, 17.9 s |

A hand on the pelvis now holds nearly every body (everything hangs from the pelvis, so its chain and subtree are the whole body: the
reader's note above); a forearm is pulled free most easily, a person's arm being the easiest thing to free. Whether a hand at the hips
should hold that firmly is for the owner (a falloff along the subtree, `hold_chain_falloff`, would soften it; not tried).

### R2, step 4: a hit takes authority by its impulse (26 November, decision 20; the owner away; behind `getup.hit_by_impulse`, 0)

The plan: `docs/recon/r2_step4_plan.md`. Behind a switch that is 0 in the files: with it at 0 every hash of `walk` stays.

**What it does** (`getup.hit_by_impulse` 1, while led): every hit the body feels takes `impulse / carry_yield_ns` of the world's
pull (all of it at 60 N*s; the parts of a blow add up), and no attempt is given up for the size of a hit: the blow moves the body and
the get-up's own rules decide (thrown off its clip by `lost_cm`, standing still too long). The share comes back by its own clock,
`getup.hit_back_s` (0.6 s for a whole share), and with `getup.hit_overflow` 1 what a blow takes beyond the whole share is owed: a
blow of 200 N*s keeps the world off for 2.33 times the clock. Measured in every mode (read only): `control.get_up.hit_off_cm`, how far
the struck body's centre of mass gets from where the carry has it within a second of a hit; and `hit_free_min`.

**A blow of 200 N*s while getting up** (`getup_hit_strong`, at 8.5 s, 0.3 s before the clip's end):

| Variant | What follows the blow |
| --- | --- |
| Switch 0 (today) | given up at the hit by the threshold ("hit with 200 N*s while it was led"), falls, stands at the second attempt |
| Switch 1, the share stops at 0, back in 0.4, 0.6, 1.0, 1.2 or 1.5 s | **the world puts it back**: the chest is thrown 42 to 58 cm off its target, the pelvis less than 40, the share comes back and the get-up ends standing at the first attempt |
| Switch 1, the share stops at 0, back in 2.0 or 3.0 s | falls, but on the edge: the attempt is given up with the pelvis 41 and 40 cm off its clip (`lost_cm` 40) |
| **Switch 1, the excess owed (`hit_overflow` 1), back in 0.4, 0.6 or 1.0 s** | the pelvis is thrown 43 cm off its clip 0.17 s after the blow, the attempt is given up by `lost_cm`, the body falls and stands at the second attempt at 14.58 s, the same at every clock |

**A push of 30 N*s while getting up** (`getup_push_weak`, at 8.5 s): with the switch on it takes half of the pull (`hit_free_min`
0.50), and the push moves the body as it did: the chest 2.4 cm off its target (2.1 at 0), the pelvis's path within 1 cm of the
switch-0 run's, both standing at 8.79 s and stepping at 9.39 s. The push comes 0.3 s before the clip's end, so the same push was moved
into the middle of the get-up (scratch scenarios, at 6.5, 7.0 and 7.5 s): the chest 6.3, 5.0 and 4.2 cm off (6.3, 4.6, 4.2 at 0), the
pelvis alike. Half of the carry's springs is still a spring of 200 N/m per kg damped near critically: a push of 30 N*s is taken back
within a tenth of a second. Only when the push takes all of it is it seen: `carry_yield_ns` 30 gives 6.3, 15.7 and 6.7 cm, and with the
share back in 1.5 s, 9.5, 22.5 and 11.1 cm (the pelvis still moved alike, the trunk bent with the push). Left at 60, the owner's
figure (decision 33); the owner compares on the videos.

**Scenarios.** `getup_hit_strong_yields` and `getup_push_weak_yields`: the two with the switch on by a console event at 0 s. The strong
one asks for a second attempt and all of the pull taken (`hit_free_min` at most 0.01) with the chest thrown 30 cm or more; the weak one
for half of the pull taken (0.45 to 0.55), one attempt, standing. Broken on purpose (the console event taken out): both fail on
those measures, and their hashes are then the old scenarios' (`98ccae2f`, `9c483753`). When the switch goes on in the files, these
expectations go to the old two and the copies go (decision 35).

**Checks.** Build without warnings; 51 unit tests. The suite at 0 against `walk`: 132 with the same hashes, 2 new (the copies, passing),
133 of 134 with `grab_getup_held` known. With the switch on (`-Tune "getup.hit_by_impulse=1" -Label r2s4_on`): every scenario meets
its expectations but the two copies' `tuning_changes` (the override makes their console event change nothing: 0 of 1), and 7 have
other hashes, each with something felt as a hit while led, each ending as before but the one meant to: `getup_hit_strong` (now falls by
the pelvis thrown 43 cm off its clip, not by the threshold), `getup_push_weak` (half of the pull taken), `getup_held` (two scripted
hands arriving on the body: 0.29 taken, the chest 36 cm off its target under their press, standing at the first attempt as before),
`getup_pressed`, `getup_pressed_one_hand`, `getup_pressed_two_hands` (the onset of each scripted press is felt as a hit: 1.0, 0.21 and
0.57 taken; held down, given up and standing as before) and `room_movable_fall_gets_up` (a piece of furniture: 0.15 taken).

**Videos for the owner** (`Tools\film-pair.ps1`, one camera, real time, the switch at 0 on the left): `Saved\Films\r2s4_hit_pair`
(`getup_hit_strong` from 8.2 s: on both sides the crouching body is thrown over backward, rolls and gets up again; on the right it is
let go by the mechanics 0.17 s after the blow instead of at once, and the two differ little to the eye); `Saved\Films\r2s4_push_pair`
(`getup_push_weak`: the push at 8.5 s, the same on both sides to the eye); `Saved\Films\r2s4_push_yield_pair` (the push moved to 7.0 s,
in the crouch, both sides with the switch on: all of the pull at 60 N*s on the left, at 30 N*s and back in 1.5 s on the right; the
trunk bends further on the right, 22 cm against 5 at the chest, which the pictures show only faintly). `contact.png` beside each is
eight frames of it. The stand
on three seeds with the switch on, and with both switches on: every trial of the 150 with `r2s2_plain`'s hash (no hit while getting
up there). Decision 7: the push matrix 160 of 160 as in `walk`, the knockdowns 8 of 8.

### The night's diff read by someone who did not write it (27 November)

The whole diff of steps 2 to 4 (code, scripts, scenarios, the plans as the specification) was given to an independent reader,
without an account of it (`CLAUDE.md`: after a night of unwatched changes). Nothing found that changes a run with both switches at 0
or while measuring (the order of the calls into Physics Control kept, every new factor exactly 1, nothing in the simulation reading
the new measures). Found and put right:

- **`Tools\film-pair.ps1` could pass an old video for a new one**: it checked only that `film.mp4` and `pair.mp4` existed, in folders
  that are the same on every run; a side whose run failed (a mistyped `-TuneB`) would have shown the last film under the new label.
  Both files are deleted before they are made, a side whose run printed no `SCENARIO` verdict stops the pair, and `%` and `"` are
  taken out of the labels (ffmpeg's drawtext reads the one, the command line the other).
- **What holds left of each body's springs was carried into the next attempt** (`getup.hold_by_body` 1): the shares come back only
  while the carry fades, and an attempt given up during its ease left them part of the way; the next began with its chain partly
  unled for up to 0.4 s. Set back to 1 as an attempt begins (a hold still there takes them down again in the first frame).
- **A clip slowed to just above 0 was never given up** (a press and holds adding up to, say, 420 N: pace 0.07): it was neither
  stalled nor waiting, and crept on. A pace under a tenth now reads as a pause and is bounded by `stall_s` as one held down.
- `Tools\getup-stand.ps1` now gives each trial's time from its first attempt to its first standing (`to_stand_s`, from
  `control.get_up.first_started_s` and the log) and their median; `Tools\getup-seeds.ps1` prints it a seed (decision 13: how much
  longer a held body takes). Step 2's plan named the state `getup` where the code writes `get_up`.

**After the fixes** (`Saved\Matrix\getup_seeds\r2_fix_*`, `Saved\Suites\r2s3_on_fix`): the suite at 0 has `walk`'s 132 hashes and the
two new scenarios; with the hold on, 134 of 134 and every hash as before the fixes. One hand: 32 of 150 pull free, 144 stand in the end,
every trial as before; pressed: 52, 147, as before. Two hands: still 2 of 150 pull free, but the bodies no longer creep through one
attempt for the whole hold: an attempt held to under a tenth of its pace is given up after 3 s and tried again (attempts a fall 2.31
against 2.05, at the first attempt 2 against 31, standing in the end 146 against 147; 81 of 150 trials as before, trial 17 of the
videos among them). From the first attempt to the first standing, by the stand's new line: one hand 13.1 to 13.5 s in the median of
each seed, two hands 14.8 to 15.1, pressed with 300 N 21.7 (the hold lasts 15 s and the press 20).

Left, and written down: the parts of a blow add up only roughly under decision 20 (each flinch is judged once; what is left of a blow
below `thresholds.flinch_impulse_ns` when its window closes is dropped, and parts in opposite directions cancel); an attempt given up
in its very first frame is counted neither led nor not led (`led_attempts + not_led_attempts` can be less than `attempts`; the check
cannot fail falsely by it); with `hold_chain` 3 a hand on the pelvis takes the carry off the whole body at 150 N (everything hangs from
the pelvis; decision 31 as written); `control.authority` counts the frames after the settle time, `assist_force_share_mean` every
get-up frame (equal unless a get-up begins before it).

### R3, step 1: the way and the time of a fall, sensed (27 November, 02:40; measure only)

After the owner's list and every step of R2 that needs nothing of the owner (`docs/recon/r3_step1_plan.md`). Every tick while
Falling the controller works out `FSandboxFallSense` (read only): the way over the floor (the centre of mass's velocity and the
pelvis's lean, as the fall's pose is chosen) and the time until the trunk meets the floor (the chest's centre of mass under free fall,
the trunk taken as a ball of 12 cm, `FSandboxBodyProbe::TimeToFloorS`, unit-tested, 52 unit tests) or what a ball swept along the
chest's horizontal velocity meets first (a wall, a piece of furniture). The runner keeps the first fall's sense against what happened:
`control.fall.sense` (`foreseen_s`, `impact_s`, `first_error_s`, `mean_abs_error_s`, `other_than_floor`, `way_error_deg`). The suite
has every hash of `r2night`, the knockdowns 8 of 8 as before, the stand of 150 as before.

**Read by an independent reader, and put right before the figures below:** the sweep asked only for the static and moving world, not for
furniture with mass (a `PhysicsBody`): it does now; a fall whose trunk already touched something on its first tick was not recorded and
a later fall was taken for the first: the first fall is recorded whatever; the model foresaw the chest alone while the trunk's touch is
any part of it: the first of the pelvis, the chest and the head now; ticks held up off the floor counted: they are left out; and the
way was compared with the centre of mass's travel to the end of the run, the get-up included: now to the trunk's touch, and not for a
body that went less than 5 cm. Without gravity and moving up the time is held to 10 s.

**What it foresees** (the stand of 150, `Saved\Matrix\getup_seeds\r3s1_sense2`; the suite with every hash of `r2night`, the knockdowns 8
of 8): the trunk meets the floor 0.28 s after the fall begins in the median (0.06 at the 10th percentile, 0.43 at the 90th); at the first
tick the sense foresees 0.39 s, late by 0.055 s in the median (0.32 s at the 90th, early by 0.026 s at the 10th): a body that topples
about its feet, and is pushed by its blow, brings its trunk down faster than a free fall. Over the whole fall each tick's foresight is
off by 0.046 s in the median (0.20 at the 90th): it closes in as the trunk comes down. Something other than the floor was foreseen
first in 9 falls. The way foreseen at the first tick against the way the centre of mass went until the trunk touched (79 cm in the
median): 2.5 degrees in the median, within 45 degrees in 121 of 143.

**The meeting swept along the fall's own path** (03:20): the late foresight was the sweep's, not free fall's. The horizontal sweep went
along the chest's horizontal velocity at the chest's height and so missed a table's top or a seat the trunk comes down on. Each of the
pelvis, the chest and the head is now swept along the chord of its own free-fall path to 2 cm above the floor (so that the floor itself
is not met; on the bare floor nothing else was foreseen in 78 of 78), and a hit at a share of the chord is that share of the time. On the
stand of 150 (`Saved\Matrix\getup_seeds\r3s1_sense3`; the suite with every hash of `r2night`, the knockdowns 8 of 8, 52 tests): at the
first tick late by **0.016 s** in the median (early by 0.091 s at the 10th percentile, late by 0.189 at the 90th); each tick's foresight
off by **0.025 s** in the median (0.070 at the 90th). By the kind of fall, the first tick's error in the median and something other than
the floor foreseen first: the bare floor 0.026 s, none of 78; a wall 0.018 s, 17 of 24; the table early by 0.083 s, 17 of 24; the chair
0.001 s, 14 of 24. The knockdowns' falls (a blow of 200 N*s, the bare floor): early by 0.01 to 0.12 s at the first tick (a fall that
the blow carries over the floor comes down later than one from rest), 0.006 to 0.054 s off over the fall; one ends without the trunk
touching.

**Read again (04:20) and put right:** one chord sags below the free-fall arc by up to a quarter of the fall's height, and met low
furniture the arc clears (the table's foresight early); the path is now two chords through the middle of its time. On the same stand
(`Saved\Matrix\getup_seeds\r3s1_sense4`; every hash kept, the stand trial for trial as `yaw_plain`): late by **0.020 s** at the first
tick in the median (early by 0.080 at the 10th percentile, late by 0.200 at the 90th), **0.026 s** off over the fall (0.067 at the
90th), the way 2.5 degrees in the median and within 45 in 121 of 143; by kind, the bare floor 0.026 s (nothing else foreseen in 78),
a wall 0.018 s (17 of 24 foresaw the wall), the table early by 0.037 s instead of 0.083 (16 of 24), the chair late by 0.030 s (14 of
24). Not swept: the player's body in a headset session (its own object type, which the body module does not know of), and a sweep that
starts inside a thing (a hand pressed into the chest) foresees it at once.

For R3's blend by the time to impact: free fall along the path is good to a few hundredths of a second from the first tick in the
median, the tails a fifth of a second; a toppling model is not needed for it. Nothing acts on the sense yet.

### The hold and the hit switched on together (27 November)

So that the owner can switch both on at once: `-Tune "getup.hold_by_body=1,getup.hit_by_impulse=1"` on the stand of three seeds and
the suite (`Saved\Matrix\getup_seeds\r2both_*`, `Saved\Suites\r2both_on`). Nobody holding: 149 and 139, 10.7% given up, every figure
of today's. One hand 32 of 150 pull free (10, 13, 9), two hands 2 (0, 2, 0), pressed 52 (12, 19, 21): the hold's figures. The suite:
every scenario meets its expectations (the two copies of step 4 but their `tuning_changes`, which an override makes 0), `grab_getup_held`
passes, and 15 have other hashes than `r2night`, each with a hit, a press or a hold while led.

**And with the start's blend at 0.3 s as well** (the configuration proposed to the owner: `getup.hold_by_body=1,getup.hit_by_impulse=1,
getup.start_blend_s=0.3`; `Saved\Matrix\getup_seeds\r2all_*`): nobody holding, 150 of 150 stand, 138 at the first attempt, 11.6% of
attempts given up (the blend's own figures); one hand 34 of 150 pull free (11, 16, 7), two hands 1, pressed with 300 N 55 stand under
it (14, 21, 20). The suite at 0 has every hash of `r2night`; with the three on every scenario meets its expectations but the two
copies' `tuning_changes`.

**The full scorecard after the night** (`Tools\natural.ps1 -Label r2_night_full -Baseline before_track`, with the stand of 150): the body
in the files is `walk`'s, so its figures are those of before the night: C2 1.16 of the weight, C19 11% of 169 attempts given up and a
pose held up to 3.0 s, C20 48, 44 and 47 at the first attempt (the proposed 45 a seed missed on seed 2), C21 3.2 s, C25 439 N*m.
Again at 04:25 on the body after step 7's fix (`Saved\Natural\r2_night_final`): C2 1.16, C19 14% of 175 attempts given up and a pose
held 2.8 s (the table's trials, step 7), C20 150 of 150 standing and 48, 44, 48 at the first attempt (140 of 150, decision 16's 135
met; the scorecard's 45 a seed missed on seed 2), C21 3.2 s, C25 439 N*m (unchanged).

### R2, step 5: the carry's damping kept from pushing down (27 November, decision 30's finding; not met, taken out)

Not on the owner's list for the night; tried because it needed nothing of the owner (`docs/recon/r2_step5_plan.md`). Behind a switch,
`getup.carry_never_down`: before each step the reference's vertical velocity handed to each carry control was raised just enough that
the spring and its damping together asked for no downward force (the target velocity at the centre of mass at least `v - e /
damping_s`). With it at 0 the suite had every hash of `walk`. On `getup_back` it did what it was for: the world's force 0.97 of the
weight instead of 1.12, 0 downward instead of 0.17, the plank 0.01 s instead of 0.20, the get-up as before (3.2 s, the same shape).
On the stand of 150 (`Saved\Matrix\getup_seeds\r2s5_*`, the hold's switch at 0):

| Stand | Switch 0 | `carry_never_down` 1 |
| --- | --- | --- |
| Stand, at the first attempt | 149, 139 | 149, **136** |
| Attempts given up (C19) | 18 of 169 (10.7%) | **34 of 184 (18.5%)** |
| ... the pelvis lost from its clip / held up off the floor / the clip waiting | 10 / 0 / 7 | **25 / 6** / 3 |
| C27, the left clip's trunk at 70 cm | 62.9 degrees | **81.2** |
| Authority: mean, up, over the floor, down | 1.16, 0.45, 0.91, 0.12 | 1.12, 0.49, 0.88, 0.00 |
| One hand / two hands pull free; pressed, stand under it (the hold's rule off) | 119 / 14; 50 | 132 / 22; 58 |

The downward part of the damping was braking the overshoot that the carry's upward springs cause (up to 2 weights a body): without it
a body rising faster than its clip is thrown on, six times off the floor ("held up"), and 25 attempts lose the clip. The downward push
measured in step 2 is the price of pulling up harder than the body follows; it goes with what makes the reference lie below the floor
and pull up so hard (R2's fit to the floor, R7's authority down), not by taking the brake off. Taken out (code and value; the suite's
hashes as before, `getup_back` `bf3da48f`); decision 30 is unchanged.

### R2, step 6: the start blended from the lying pose (27 November, decision 22; behind `getup.start_blend_s`, 0)

Not on the owner's list for the night and not among the steps kept back; decision 22 is the owner's (`docs/recon/r2_step6_plan.md`).
With `getup.start_blend_s` above 0 a get-up starts from where the body is: the clip layer's "from" pose is each bone's local rotation
as the simulation has it as the attempt begins, and it crosses to the clip's first pose over that time (the cross the layer already had
between two clip poses, with a time of its own); the carry's targets go from where each body lay to where the clip has it by the same
smoothed share. 0: the first pose taken whole, as before; the suite at 0 kept `walk`'s 132 hashes.

**What it does to the targets.** The muscles' target jumps at a get-up's first frame from the lying pose's to the clip's first pose,
while their strength is still 0: the largest of a run, 55 to 80 degrees on the four `getup_*` (an upper arm or a thigh). With the
blend it jumps only from the lying pose's targets to the body's own pose, 30 to 47 degrees, and the command (N*m) does not jump either
way (the strength comes up over the ease). C19's pose held still while getting up, median of the stand, 0.59 s (the clip waiting for
the ease with its first pose) becomes 0.00 s at 0.6 and 0.30 s at 0.3.

**The stand on three seeds** (`Saved\Matrix\getup_seeds\r2s6_*`, the hold's and the hit's switches at 0):

| Stand | 0 (today) | 0.3 s | 0.6 s |
| --- | --- | --- | --- |
| Stand, at the first attempt | 149, 139 | 150, 138 | 149, 141 |
| Attempts given up (C19) | 18 of 169 (10.7%) | 20 of 173 (11.6%) | 25 of 177 (14.1%) |
| ... the pelvis lost from its clip | 10 | 11 | 23 |
| Authority, mean of the body's weight | 1.16 | 1.17 | 1.19 |
| C27: trunk, farther foot (median) | 60.1 degrees, 24.9 cm | - | 59.3 degrees, 24.9 cm |
| One hand / two hands pull free; pressed, stand under it (0.6; the hold's rule off) | 119 / 14; 50 | - | 119 / 13; 50 |

At 0.3 s within the stand's noise of today's; at 0.6 s two more first attempts and more attempts lost (the targets of the carry lag
the clip during the blend, and the pelvis is more often 40 cm off as the clip starts). The suite with 0.6: every scenario passes, and the 15 with a
get-up, and only they, have other hashes. Video for the owner: `Saved\Films\r2s6_start_pair` (`getup_left` from 5.7 s, the start; on the left the body
turns from its front onto its side at once, on the right over the blend). Left at 0 for the owner's eye (decision 36).

Read by an independent reader (the diff of this step alone): nothing that changes a run at 0, and the "from" pose is the body's own (the
component-space pose the controller reads at its tick is the last physics step's, the same instant the bodies are read at; the bone
order is the clip's own). Put right: a start that could not read the body's pose went the old way without a word; it now says so in
the log. Written down: the blend reads `getup.start_blend_s` in the Lying state's tuning (as `getup.carry` is read when an attempt
begins); the clip-following path's pelvis and feet assists (carry 0) still aim at the clip's first pose from the first frame (led, their
springs are 0).

### The bootstrap at the end of the night (27 November, 00:45)

On the tree as it is left for the owner (steps 2 to 4 and the review's fixes; step 5 taken out; both new switches at 0): all 15 steps
passed in 325 s. The build, 51 unit tests, the suite 133 of 134 with `grab_getup_held` known (49.8 s; `walk`'s 132 hashes and the two
new scenarios), the push matrix 160 runs, the knockdowns 8 of 8, the stand of seed 1 50 of 50 (48 at the first attempt), the session's
self-test 14 of 14, the record and replay 13 of 13, its three takes, the grab demo, the walk with its replay. Run again at 01:20 with
step 6 in the tree (its value at 0): all 15 steps in 333 s, every figure the same. And at 02:05 with the seven phase scenarios and
the clip's own C27 measure: all 15 steps in 330 s, the suite 140 of 141. And at 02:50 with R3's first measure and the stand's tools
read and put right: all 15 steps in 329 s, the suite 140 of 141 with every hash of `r2night`.

### R2's gate today: C19, C20, C21 and C27 on three seeds (26 November, the night)

The base the end of R2 is judged against (decisions 16 and 21), read from the stand of 150 falls on the body in the files
(`Saved\Matrix\getup_seeds\r2s2_plain`; `timing_1126` gives every figure the same). Nothing changed for it. C19 is read from
`metrics.stillness.target_still_longest_by_state_s.get_up` (the longest the pose asked of the muscles stood still while getting
up) and the attempts given up; C21 from the run's log, from the first attempt's start to the first standing.

| Criterion | Limit (decision 16, 21) | Today | Met |
| --- | --- | --- | --- |
| C19, the longest pose held while getting up | 1.0 s | median 0.59 s, at most 2.98 s; longer than 1.0 s in 5 of 150 falls | no |
| C19, attempts given up | at most 10% | 18 of 169 (10.7%) | no |
| C20, stand; at the first attempt | 95%; 135 of 150 | 149 of 150 (99.3%); 139 | yes |
| C21, from the first attempt to standing | a median of 2.6 to 4.2 s | 3.20 s (the 10th and 90th percentiles both 3.20: the led clip's own length) | yes |
| C27, trunk from upright and farther foot from the pelvis as the pelvis passes 70 cm, the median of 149 | 45 degrees, 30 cm | 60.1 degrees, 24.9 cm; both met in 65 of 149 | no (the trunk) |
| C27 by clip: from the back (30), the front (39), the left (44), the right (36) | each 45 degrees, 30 cm | 29.7 / 19.6, 22.1 / 25.0, **62.9** / 28.7, **61.2** / 14.3; both met in 26, 39, 0 and 0 | the back and the front; not the sides |

Since the night `Tools\getup-seeds.ps1` prints this gate itself, over all its seeds ("R2's gate: C19 ... C27 ... by clip"; each
trial of `Tools\getup-stand.ps1` keeps its clip, `rise_trunk_deg`, `rise_feet_cm` and `held_pose_s`): run on the body in the files it
gave every figure above to the digit (`Saved\Matrix\getup_seeds\r2_gate_tool`). Read by an independent reader with the scripts'
`-TuningDir` (new in `film.ps1`, `film-pair.ps1`, `getup-stand.ps1`, `getup-seeds.ps1`): put right, the stand's `-Film` did not pass
the tuning directory on (its sheets would have shown the files' body), the stand's result and the seeds' footer did not say which
directory was used, the gate's median took the lower of the two middle values where the scorecard takes their mean, a C19 with no
pose measured printed 0, and a directory given with a trailing backslash broke the arguments. Checked once more on seed 1's floor
with a trailing backslash (26 of 26, the gate's line as expected).

What R2's matcher and fit to the floor have to move: the side clips' lean at the start of the rise (C27) and the clip's end on one
foot (most of C19's long holds and given-up attempts, the review of the audit).

**Where C19's attempts are lost, by clip** (the same stand, read from the runs' logs): of 169 attempts 18 were given up. Ten lost the
pelvis (40 cm from the clip), eight of them within the first quarter second of the attempt, at the clip's time 0, six on the clip from
the front (48 attempts there, 39 standing): the clip's first pose and where it is laid were too far from how the body lay, which is the
matcher's and the start's to put right, not the clip's run. Seven waited at the clip's end (2.6 s of it) for a body that was not upright
on both feet (three on the left clip, three on the right, one from the back). The five falls with a pose held over 1.0 s: two on the
back clip, two on the front, one on the left.

**C27's lean on the side clips is the recording's own.** A measure added for it (read only; the stand of 150 with every trial's hash
as before, `Saved\Matrix\getup_seeds\r2_clipshape`): `control.get_up.rise_clip_trunk_deg` and `rise_clip_feet_cm`, the trunk and the
farther foot of the reference itself (the carry's targets) as its own pelvis first passes 70 cm. In the median, by clip:

| Clip | Get-ups | Trunk: body / clip | Farther foot: body / clip |
| --- | --- | --- | --- |
| From the back | 30 | 29.7 / 30.7 degrees | 19.6 / 21.8 cm |
| From the front | 39 | 22.1 / 22.0 | 25.0 / 21.3 |
| On the left side | 44 | **62.9 / 57.1** | 28.7 / 29.4 |
| On the right side | 36 | **61.2 / 63.4** | 14.3 / 8.8 |

The body follows its clip to within a few degrees; the side clips rise with the trunk 57 to 63 degrees from upright, as the person
recorded did. So C27's 45 degrees (decision 21) cannot be met on the sides by following these two recordings better: it needs another
way up from a side (the matcher rolling the body to its back or front first, or other clips), or a limit for the side clips of their
own. For the owner with R2's matcher; not decided here.

**The same stand with the side clips taken out** (a tuning directory whose `getup.clip_left` and `clip_right` are empty, so a body
lying on its side takes the nearer of the clips from the back and the front; `Tools\getup-seeds.ps1 -TuningDir`, new;
`Saved\Matrix\getup_seeds\r2_sides_bf`; the directory kept as `Saved\Films\inputs\tuning_sides_bf`):

| | All four clips (today) | Back and front only |
| --- | --- | --- |
| Stand, at the first attempt | 149, 139 | 150, 137 |
| Attempts given up (C19) | 18 of 169 (10.7%) | 28 of 179 (15.6%) |
| C27, both limits met; trunk and farther foot, median | 65 of 149; 60.1 degrees, 24.9 cm | **139 of 150; 22.3 degrees, 24.8 cm** |
| Authority, mean | 1.16 | 1.26 |

C27 is met, at the price of more attempts lost and a tenth of a weight more from the world, which rolls the body from its side onto its
back or front into the clip's first pose (there is no roll of its own: that is the matcher's step). Video: `Saved\Films\r2_side_clips_pair`
(the stand's trial 6 of seed 1: on the left the body pushes up from its side on its arms, the trunk low; on the right it is rolled onto
its back, sits up, crouches and rises upright). A measurement for the owner's choice with the matcher, nothing changed.

**A get-up that begins while the body still settles** (R2's item; `getup.rest_s` 1.5 against 1.0 and 0.8, `-Tune`): standing 150 and
149, at the first attempt 137 and 134, attempts given up 16.1% and 22.1% (10.7% today): the body that has not settled loses more
attempts. Left at 1.5.

**C19's poses held over 1.0 s are the clip's end by a wall or the table** (looked at after step 7, `Saved\Matrix\getup_seeds\r3s1_sense4`;
the body trial for trial as `yaw_plain`): five of the six hold 2.98 s, each "the clip stood at 2.60 s for 3.0 s waiting for the
body", two at a wall and three at the table. In seed 2's trial 38 the pelvis is 1 cm from the clip's and the feet 4 and 5 cm, but the
floor carries the left foot with 506 N and the right with 0: the right foot's body hangs 0.7 cm over the floor (8.7 cm against 8.0
standing, the trace's checkpoints) for the whole wait, held there by its clip pose, and the body stands on one foot and the world's
pull (0.45 of the weight) until the attempt is given up. In seed 1's trial 40 both feet are 5.5 to 6.6 cm up, on the table's rail. The
first is the fit to the floor's case (a foot the clip has on the floor aimed at it with no vertical pull), the second the matcher's.

**C19's lost attempts are the table's.** By the kind of fall on the same stand: the bare floor, 1 of 80 attempts given up (1.2%; 76
of 78 at the first attempt); at a wall 2 of 26; at the chair 1 of 25; **at the table 14 of 38 (36.8%; 18 of 24 at the first attempt)**.
The seven attempts that waited at the clip's end were all at a wall or the table (the body standing against it, a foot held off).

**The pelvis lost at the start is a body lying on the furniture.** Of the ten attempts that lost the pelvis, nine were at the table
(eight) or the chair (one), one on the bare floor; five were given up in the very frame they began, the pelvis 70 to 98 cm from the
clip's: the body lay on or across the table, and the clip is laid on the floor the body last stood on (`StartGetUp`, the frame's height
`LastSupportHeightCm`). Seed 2's trial 40 began and gave up three times so before another way took. A get-up laid on the surface under
the body is R2's fit to the floor (and its scenario "a body laid across a thing"), which the owner kept for later; written down here as
what it has to put right first.

### A push, a blow and a hold in the get-up's phases (27 November; C26, R2's scenarios)

R2 asks for "a hit, a push and a hold in each phase" of the get-up (C26). Seven scenarios on `getup_back`'s fall and get-up (the clip
from the back begins at 5.59 s, eased in by 6.19), each a scripted impulse of 30 N*s (`push`) or 200 N*s (`hit`) on the chest from the
side as the body sits up (6.8 s, the pelvis 16 cm up), is on its knees (7.6 s, 50 cm) and rises (8.1 s, 80 cm), and a hand that takes
hold of the chest as it sits up and holds it 2 s (`getup_phase_sit_hold`; the chest still there, so a scripted hand can find it: in the
later phases it moves 50 cm a second, faster than a hand sent to a point can follow, and no hold is written for them). They ask for
today's behaviour: a push does not end the get-up (one attempt), a blow does (two), and the body stands at the end; the hold, a grab
and standing. And that each lands while the body gets up (an independent reader's point: a push after the body stood passed as well):
`control.get_up.hits` at least 1, a push's struck body at least 1 cm off its target (`hit_off_cm`), the hold's grip 40 frames or more
and `held_s` at least 0.3 s. Broken on purpose (the rising push moved to 12 s, after standing): it fails on both measures. In the suite (141 scenarios, `walk`'s 132 hashes kept; saved as the baseline `r2night`).

Each run with the night's switches on, one at a time:

| Scenario | Switch 0 | `hold_by_body` 1 | `hit_by_impulse` 1 | `start_blend_s` 0.6 |
| --- | --- | --- | --- | --- |
| push, sit / knees / rising | one attempt; the chest 4.7 / 4.2 / 3.2 cm off its target | the same | half the pull taken; 5.0 / 4.4 / 3.4 cm | the same |
| blow, sit / knees / rising | given up by the threshold at the blow, standing at the second | the same | thrown off the clip (the pelvis 40 to 42 cm, the chest 80 / 49 / 58 cm), standing at the second | the same |
| hold, sitting up | the grip torn off after 75 frames (0.8 s) | **held the whole 2 s** (188 frames) | 0.17 of the pull taken as the hand arrives; torn off as at 0 | as at 0 |

Every one passes with each switch.

### How long the body lies by how it fell (27 November; R2's item, values only, not taken)

The values exist (`getup.rest_per_ns_s`, `rest_max_s`, both 0: every body lies `rest_s`, 1.5 s). Tried on the stand of 150 with
`-Tune`, nothing else changed:

| `rest_per_ns_s`, `rest_max_s` | Lay before the last attempt, median (10th, 90th) | Stand, first attempt | Attempts given up |
| --- | --- | --- | --- |
| 0, 0 (today) | 1.90 s (1.49, 2.68) | 149, 139 | 10.7% |
| 0.005, 3 | 2.70 s (1.68, 3.73) | 150, 138 | 14.7% |
| 0.01, 4 | 3.58 s (1.77, 4.81) | 149, 137 | 16.2% |

Lying longer after a harder fall gives no better get-up here (more attempts lost, the same table and chair falls); whether it reads as
more human is the owner's eye, not a figure. Left at 0.

### What the led get-up's 19 controls cost in the physics step (26 November, the night; R1's follow-up)

`Tools\sweep.ps1 -Scenarios "getup_back,getup_front,getup_left,getup_right,stand_npc" -Vary "getup.carry=0,1" -Parallel 1`, twice
(`Saved\Sweeps\carry_cost_a`, `_b`; one engine process, nothing else running): the mean physics step of the four get-ups is 0.565 to
0.583 ms with the carry off and 0.654 to 0.667 ms with it on, 0.087 ms more (15%); the median step 0.545 against 0.614, and the 99th
percentile 0.93 to 0.98 against 1.07 to 1.17. `stand_npc`, which never gets up, 0.570 to 0.577 either way. The median moving as much
as the mean says the cost is in most frames, not only the 3.2 s of the get-up: the controls are made at the first led get-up and kept,
disabled, for the rest of the run (`FSandboxCarry`), and a disabled control still costs about 0.1 ms a frame (two substeps). Well
inside the 1.5 ms of the suite's limit; making them only while led, or taking them down after a get-up, would be measured against the
hashes first (a constraint made or taken down moves the solver's order). Not changed.

### The scorecard after the night (26 to 27 November)

`Tools\natural.ps1 -Label r2_night -Quick -Baseline before_track` (56 s; `Saved\Natural\r2_night`): with both new switches at 0 the
body is the one of `walk` (every hash), so nothing a criterion reads of it moved. C25's largest jump is still 439 N*m (calf_l,
`hit_chest_200_side_falls`), the owner's baseline; the largest target jump of 83.5 degrees is in the new `getup_hit_strong_yields` (a
blow thrown into a get-up), below before_track's 127. C2 1.08 of body weight (the four `getup_*`), C19 and C21 within their proposed
limits on the four get-ups (the stand above is the gate's figure), C20 not run with `-Quick`.

### R2, step 7: get-ups from eight ways of lying, and the support frame's seam (27 November, 03:40; the owner away)

`docs/recon/r2_step7_plan.md`. The first of R2's scenarios: get-ups from eight ways of lying. The four `getup_*` lay the body by a blow;
a body spawned lying instead (`body.rotation_deg`) takes that for its standing pose (the back and the front stood lying in a stagger, the
others gave up with the clip 40 cm from the pelvis), so a rig lays it after its control starts: `body.laid_deg` (`Scenarios/README.md`),
every body turned about the spawn point and raised to 2 cm over the floor, no velocity; the controller finds it falling at 0 s. Eight
scenarios `getup_laid_<way>` (0, 45, ... -45 degrees about the length: back, back_right, right, front_right, front, front_left, left,
back_left), each to stand at its first attempt.

**Found with them:** on the front and the front-right the body stood up and, a second later, "the feet stand 65.0 cm from their
stance; foot_r steps back to its place", turned round and fell; the film (`Saved\Films\getup_laid_front`) shows it standing with its
feet together. `USandboxBodyController::SupportFrame` took the mean of the two feet's turns since the start, each from -180 to 180
degrees; standing about half round from where it started, the feet were at about 175 and -175, the mean 0, the support frame half round
from the feet: the stance the other way (2 x 32.5 cm), and the anchors of the pelvis, the chest and the head turned half round. Each
later foot is now taken on the first one's side of the seam (a body whose feet do not straddle it sums the same to the bit).

**What changed:** the suite against `r2night`, 148 of 149 (`grab_getup_held` known): every hash but `getup_pressed_one_hand`, which
stood, staggered and fell again and now stands (states `...get_up>standing>stagger>falling>lying>get_up>standing>stagger>standing` to
`...get_up>standing>stagger>standing`; rest after the fall 25.9 to 17.7 s, energy 25.5 to 14.2 J), and the eight new scenarios, each now
standing at its first attempt (before the fix the front and the front-right needed two). The clips chosen: B from the back and both
backs between, F from the front and both fronts between, R and L from the sides; the trunk as the pelvis passes 70 cm: 30 degrees (B),
22 (F), 61 (R), 80 (L); the world gives 0.89 to 1.14 of the weight. The push matrix: 160 the same as `walk`; the knockdowns 8 of 8,
the same. The stand of 150 (`Saved\Matrix\getup_seeds\yaw_*`): 6 trials of 150 differ from before (the others to the bit): a fall on the
bare floor stands at its first attempt instead of its second, and five at the table, where 37% of attempts are given up anyway, come out
otherwise (2 to 4, 5 to 3 attempts and now standing, 3 to 3, 4 to 9, 3 to 5). In all 150 of 150 stand (149), 140 at the first attempt
(139), but 25 of 175 attempts given up (18 of 169), from those five table trials. Held by one hand 118 of 150 pull free (119 at step 0),
two hands 16 (14), pressed with 300 N 51 stand under it (50). Kept, without a switch: a defect, acting only where the feet straddle the
seam; the table's chaos is the matcher's and the fit to the floor's. With the three switches proposed to the owner on
(`getup.hold_by_body=1,getup.hit_by_impulse=1,getup.start_blend_s=0.3`, `Saved\Suites\laid_on`) the eight stand at their first
attempt as well; the suite fails only the two copies' `tuning_changes`, as before.

**By the furniture** (the room of the stand, `Specs/rooms/getup_room.json`; films with a camera inside the walls, `-Camera`, or the
pictures are black). On its back and on its front along the table's long side, 40 cm from its edge: both stand at the first attempt
(B and F), now `getup_laid_beside_table_back` and `_front` in the suite. The rest, kept as files for the fit to the floor
(`docs/recon/r2_step7_furniture`), not in the suite: **under the table** on its back, the trunk under the top: 4 attempts, standing at
the fourth, and in a stagger at the end of 25 s; **on the table's top** on its back, the legs over its end: it gets up on the table
and stands on it (the carry never pulls down, so the clip laid on the floor 77 cm below lifts it as far as the table lets it;
`Saved\Films\getup_laid_on_table_back`); **across the table's end** on its front, the pelvis over the top: 4 attempts, the first
three given up ("the pelvis is 40 cm from where the clip has it") as the legs, held to the clip's pose, flail in the air, then it
slides off and stands (`Saved\Films\x_getup_laid_across_table_end_front`); on its back the same way: 10 attempts, none standing ("the
pelvis is 74 cm from where the clip has it"). These are the stand's table trials made deterministic. The rig does not look for what is
in the way: a body laid into the chair's back and the table's edge (tried first, across the chair) was thrown out of them.

**Read by an independent reader** (the step's diff and SenseFall, 04:15): the rig's turn is right for every way, what the runner and the
controller read just after it is the laid pose, and the seam's fix sums the same values in the same order where the feet do not straddle
it. Put right: a laid body without `location_cm` at z 0 would have its controller take 150 cm for the floor, and a kinematic one would
be put back standing: both are now refused when the file is read. Written down, not changed: laid on the back or the front the body
has no way over the floor at its first tick and takes the backward fall's pose for the half second before it lies; the bounds that
raise it are boxes, so its shapes start a few cm higher than `laid_height_cm` at most. **The same seam in the gaze**, found by the
reader: the head's yaw toward a target was its angle from the chest's front, from -180 to 180, clamped to the head's limit, so a target
straight behind swung the head from one limit to the other as it crossed; behind the shoulders the target is now also taken a turn the
other way round (190 for -170) and is, when the head is nearer to that (read a third time: a first rule that kept the whole rear half
on the head's side turned the head away from a target just behind a shoulder). Both seams are pure arithmetic now
(`FSandboxBodyProbe::OnSideOf`, `GazeYawDeg`) under a unit test (`DictatorSandbox.Body.Probe.AnglesAcrossTheSeam`), and the rig's turn
under another (`DictatorSandbox.Scenario.Spec.LaysTheBodyDown`); 54 unit tests. Every hash of the suite kept (no scenario has a
target behind), `Tools\record-replay.ps1 -Perception` 12 of 12. Side by side, 20 runs each of `getup_laid_front`,
`getup_laid_back_right` and `getup_laid_beside_table_back` in 8 processes: one hash each (the rig lays the body before the first step,
so the solver's rebuild is in step as in any run). The long suite (`-Long`): `stand_npc_10min` passes. The bootstrap at 04:33: all 15
steps in 332 s, the suite 150 of 151, 54 unit tests.

### R3, step 2: the fall's arms blended by its way (27 November, 04:40 to 05:00; behind `fall.blend`, 0; decision 37)

`docs/recon/r3_step2_plan.md`, its first part. While the body falls, the layer the get-up's clip uses holds a blend of the sample's four
reaching poses (`M_ragdoll_reach_forward`, `_back`, `_left`, `_right`, each read at 1.0 s), weighted every tick by the sensed way
(`FSandboxFallSense`) in the body's frame, by the squares of the way's parts (a way at 45 degrees is half forward, half to a side), the
weights following over `fall.blend_follow_s` (0.1 s), the layer coming in over `poses.fall_blend_s`; lying, the layer goes over the
same time; a get-up's clip takes the layer over. `fall.blend` 0: the suite has every hash of `laid`, 54 unit tests.

**First over the whole body:** the knockdowns 7 of 8 (one rested only after 3.41 s), and the films showed a body lying with a leg held
in the air: the sample's poses are a body's in the air, its legs trailing. **Then over the arms only** (the collarbones, the upper and
lower arms, the hands; 8 joints), the legs and the trunk keeping the written fall pose:

| Knockdown (200 N*s, from) | Rest, s (0 / 1) | Energy the fall gains, J | First to touch | Head touched | Head's speed at the floor, m/s |
| --- | --- | --- | --- | --- | --- |
| 0 | 1.37 / 1.89 | 1.65 / 8.26 | hand_l / hand_l | yes / yes | 1.27 / 0.54 |
| 45 | 1.39 / 1.59 | 1.54 / 4.89 | hand_l / hand_l | yes / yes | 2.78 / 2.52 |
| 90 | 1.69 / 1.63 | 3.06 / 0.38 | lowerarm_l / hand_l | no / yes | - / 0.34 |
| 135 | 1.40 / 1.54 | 1.82 / 3.16 | hand_r / hand_r | yes / yes | 3.00 / 3.69 |
| 180 | 1.20 / 1.58 | 1.57 / 6.34 | hand_r / hand_r | yes / yes | 1.71 / 0.41 |
| 225 | 1.87 / 1.64 | 0.54 / 2.05 | pelvis / hand_r | yes / yes | 0.58 / 1.32 |
| 270 | 1.22 / 2.12 | 14.17 / 19.44 | pelvis / hand_r | no / no | - / - |
| 315 | 1.87 / 1.76 | 0.54 / 1.45 | pelvis / hand_l | yes / yes | 0.38 / 1.23 |

A hand meets the floor first in all eight (five with 0); the head's speed is lower in three, higher in three; every fall rests within
2.12 s; the most energy a fall gains 19.4 J (14.2), under the owner's 40 J, which is not touched. The suite with it on
(`Saved\Suites\blend_on`): every scenario meets its expectations, and the 54 with a fall have other hashes (every fall goes through
the blend). The push matrix: 160 ending as `walk`'s. The stand of 150 (`Saved\Matrix\getup_seeds\blend_plain`): 150 of 150 standing,
**130 at the first attempt (140), 20.6% of attempts given up (14.3%)**, 9 poses held over 1.0 s (6), C27 as before: the bodies lie with
their arms elsewhere, and the get-up's clips are chosen and laid from there. Films for the owner, 0 and 1 side by side from the side:
`Saved\Films\knockdown_spine_04_000_200_pair`, `_180_200_pair`, `_315_200_pair`, and the sheet `Saved\Films\kd000_blend1`. Left at 0.
By hand (`Tools\knockdowns.ps1 -ByHand`, two fists through the chest at 4 m/s, 6 of 8 fall with either): a hand or forearm first
in all six either way; the head's speed at the floor lower in all three falls where it touches (4.01 to 2.06, 3.88 to 3.65, 4.16 to
1.31 m/s); the energy lower in three, higher in two; one fall rests only after 3.36 s (2.53 off), over the 3 s of the criteria. The
scorecard's fall criteria (`Tools\natural.ps1 -Quick -Tune "fall.blend=1"`, `Saved\Natural\blend_quick`, against `r2_night_final`):
C14 by hand 100% (83%); C15 the head strikes in 45% of the falls by hand (55%), 63% by blows (63%), touching at all by blows in 88%
(75%); C16 the head's speed, 90th percentile, 3.88 m/s by hand (4.16), 3.69 by blows (3.00); C17 at rest within 2.12 s (1.87), 19.4 J
(14.2). Better by hand, worse by blows of 200 N*s. Films of two falls by hand, 0 and 1: `Saved\Films\shove_135_pair`, `shove_225_pair`.
**Half the blend** (`fall.blend_share` 0.5): by blows the head's speed at the floor 0.89, 0.64, 2.55, 0.87, 0.63, 0.43 m/s (0: 1.27,
2.78, 3.00, 1.71, 0.58, 0.38; 1: 0.54, 2.52, 3.69, 0.41, 1.32, 1.23), a hand first in five of eight, the most energy 16.5 J, but one
fall (315) at rest only after 3.03 s; by hand 2.20, 3.83, 1.92 m/s (0: 4.01, 3.88, 4.16), every fall at rest within 2.34 s, 25.4 J at
most; the stand 149 of 150, 135 at the first attempt, 25.0% of attempts given up (`Saved\Matrix\getup_seeds\blend_half`). Lower head
speeds than either end by blows in four of six, but no figure of the get-up better.
Held and pressed on three seeds with it on (`Saved\Matrix\getup_seeds\blend_*`; the hold's switch at 0): one hand 113 of 150 pull
free (118 with the blend off), two hands 14 (16), pressed with 300 N 42 stand under it (51), 147 of 150 standing in the end.

Read by an independent reader: at 0 nothing runs and every name keeps its value; with 1 the clip's layer and the blend's never fight
(the get-up's clip takes the layer over, a given-up get-up's clip fades before the blend comes back), the weights sum to 1, the arm
filter keeps exactly the eight joints of the arms. Put right: a body held up in mid-fall dropped the blend's layer in one tick (a jump
of the arms' targets); every end of the blend now goes over `poses.fall_blend_s`, as lying did (the knockdowns as before). Written down:
the poses are read once a body, at its first fall (a later `fall.blend_clip_s` reaches the next body); an older tuning directory
without the new names does not load (as with R2's `getup.hold_*`); `fall.aim` and `fall.blend` together would let the aim's hand
targets override the blend's arms (both 0). The bootstrap at 05:10, with the blend's code at 0: all 15 steps in 332 s. With it on,
side by side in 8 processes, 20 runs each of `hit_chest_200_side_falls`, `effector_shove_knockdown_gets_up` and `getup_laid_front`:
one hash each (the poses are read at the body's first fall, in the middle of a run).

### The whole night read once more, for how its parts meet (27 November, 05:20)

An independent reader of the whole diff, asked for the interactions the separate readings could not see. With every switch at 0
nothing. With the hold's rule on (`getup.hold_by_body`), the holds were counted only while the get-up was led: once an attempt ended
(given up, or standing) and the world's pull faded over `getup.carry_release_s`, the held chain's shares came back by the same clock and
a hand still holding a body was pulled against again; with the hit's rule on, `HitFree` came back during the fade, so a blow that felled
the body was followed by the world's pull rising again for a moment. Put right, under those switches only: the holds are counted while
any pull is left, and in the fade nothing of the pull comes back (the shares, `HitFree` and the press's share only go down). The suite at
0 keeps every hash of `laid` but `getup_push_weak_yields`, the copy that switches the hit's rule on itself; with both rules on, every
scenario meets its expectations but the two copies' `tuning_changes`, and against the same before step 7 four have other hashes
(`getup_pressed_one_hand` also by step 7's fix, `getup_push_weak`, `getup_push_weak_yields`, `grab_getup_held`, which passes). Held on
three seeds with the hold's rule on (`Saved\Matrix\getup_seeds\fade_*`): one hand 29 of 150 pull free (32 before; 20 to 60 wanted), two
hands 0 (2; at most 5), pressed with 300 N 53 stand under it (52). Written down, not changed: `CarryFree` (the press's share of the
older rule) is not reset when an attempt starts, as `CarryBodyFree` is (since R1; a press that ended just before leaves the next
attempt partly unled, mostly hidden by the 0.6 s ease); and after a given-up get-up the fall's way is read against the clip's pelvis,
not the standing one (as `ChooseFallWay` always has), which only matters with `fall.blend`. The three rules proposed to the owner on
together, after every fix of the night (`getup.hold_by_body=1,getup.hit_by_impulse=1,getup.start_blend_s=0.3`,
`Saved\Matrix\getup_seeds\final_all_*`): nobody holding, 150 of 150 standing, 140 at the first attempt, 10.2% of attempts given up
(14.3% with all three at 0), 5 poses held over 1.0 s; one hand 31 of 150 pull free (10, 14, 7), two hands 0, pressed with 300 N 56
stand under it (15, 21, 20). The quick scorecard with the three on (`Saved\Natural\final_all_quick`): C2 1.08, C19 and C21 within
their limits on the four get-ups. (C25 is read from the latest suite, the switches at 0: 439 N*m as before, and the largest jump of a
target 105.4 degrees, `upperarm_r` as the get-up of step 7's `getup_laid_front` begins at 2.00 s, 83.5 before that scenario: an arm
laid flat taken to the clip's first pose at once. Run alone with the start's blend at 0.3 s, the same get-up's largest jump is 38.7
degrees, `thigh_r`: decision 22's start takes most of it out.) Over the whole suite with `getup.start_blend_s` 0.3 alone
(`Saved\Suites\start03`, every scenario meets its expectations): the largest jump of a target is 75.2 degrees (`getup_laid_right`)
instead of 105.4, and lower in every get-up looked at: `getup_laid_front` 105.4 to 38.7, `getup_pressed` 83.5 to 33.9, `getup_front`
80.2 to 46.9, `getup_left` 75.6 to 42.3, `getup_phase_crouch_hit` 83.5 to 45.8 (decision 36's case for 0.3, beside its video).
**Every new switch on at once** (the three and `fall.blend`, the suite with `-Long`, `Saved\Suites\everything_on`): every scenario
meets its expectations but the two copies' `tuning_changes` and `getup_held`, whose right hand is snapped once
(`effectors_not_snapped`): the body, up at another moment than the scenario's hands were written for, stands on that hand (the film
`Saved\Films\held_everything`: from 10.8 s the hand lies by its foot), the hand's target moves away and at 12.3 s the gap passes the
150 cm of `effectors.hand.snap_distance_cm`. Each switch alone, the three without the blend, and the hold with the blend pass it. A
scenario whose hands follow a written path past a body that moves otherwise; for the owner, with the switches.
The bootstrap at 05:46 on the final tree:
all 15 steps in 331 s, the suite 150 of 151, 54 unit tests; the baseline `laid` saved again from it (the copy's new hashes).



## Known behaviour of the stock mannequin

- **Residual hand motion.** After the 80 N*s impulse in `impulse_passive`, the right hand keeps rotating slowly
  on the floor and the body never satisfies the rest criterion (5 cm/s, 0.2 rad/s). The scenario therefore
  has no `rest_within_s` expectation. This is a property of the stock physics asset, to be addressed by the
  project's own physics asset in M1 and M2.
- **A free constraint.** `UserConstraint_0` in the stock physics asset is not a rigid joint; the stretch
  metric skips it.
